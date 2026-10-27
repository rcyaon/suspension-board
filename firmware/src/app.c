/* Suspension logger application.
 *
 * Timing model: the ADS131M08 DRDY interrupt (1953.125 Hz at OSR 2048) is the
 * heartbeat. Each DRDY reads the bridge frame on SPI1, the IMU on SPI2 and the
 * previous MCU ADC scan, packs one 60-byte record and starts the next scan.
 * Records fill 512-byte blocks in a RAM ring. The main loop moves blocks to the
 * card on the same SPI1 bus, one short SD step at a time, and only starts a
 * step if it will finish before the next DRDY. If a DRDY lands while the card
 * owns the bus, the frame is read as soon as the step ends (flagged LATE). */
#include "app.h"
#include "ads131m08.h"
#include "fat32.h"
#include "hal.h"
#include "icm42688.h"
#include "log_format.h"
#include "sd_spi.h"
#include "util.h"

/* ---------- configuration (CONFIG.TXT on the card) ---------- */
#define CFG_MAX 3000
static struct {
    uint8_t node, master, autostart;
    uint16_t osr;
    uint8_t gain[ADS_NCH];
    uint16_t fat_date;
    uint32_t max_mb;
} cfg;
static char cfg_text[CFG_MAX + 1];

static void cfg_defaults(void) {
    static const uint8_t g[ADS_NCH] = {128, 128, 32, 32, 128, 128, 64, 32};
    cfg.node = 1; cfg.master = 1; cfg.autostart = 0; cfg.osr = 2048;
    u_memcpy(cfg.gain, g, sizeof g);
    cfg.fat_date = (uint16_t)(((2026 - 1980) << 9) | (1 << 5) | 1);
    cfg.max_mb = 512;
    cfg_text[0] = 0;
}

static uint32_t parse_u(const char **p) {
    uint32_t v = 0;
    while (**p >= '0' && **p <= '9') v = v * 10 + (uint32_t)(*(*p)++ - '0');
    return v;
}

static int key_is(const char *line, const char *key) {
    size_t n = u_strlen(key);
    return !u_strncmp(line, key, n) && line[n] == '=';
}

static void cfg_parse(void) {
    const char *p = cfg_text;
    while (*p) {
        const char *line = p;
        const char *val = line;
        while (*val && *val != '=' && *val != '\n') val++;
        if (*val == '=') val++;
        const char *q = val;
        if (key_is(line, "NODE")) cfg.node = (uint8_t)parse_u(&q);
        else if (key_is(line, "CAN_MASTER")) cfg.master = (uint8_t)parse_u(&q);
        else if (key_is(line, "AUTOSTART")) cfg.autostart = (uint8_t)parse_u(&q);
        else if (key_is(line, "OSR")) cfg.osr = (uint16_t)parse_u(&q);
        else if (key_is(line, "MAX_MB")) cfg.max_mb = parse_u(&q);
        else if (key_is(line, "GAIN")) {
            for (int i = 0; i < ADS_NCH; i++) {
                cfg.gain[i] = (uint8_t)parse_u(&q);
                if (*q == ',') q++;
            }
        } else if (key_is(line, "DATE")) {           /* YYYY-MM-DD, used for the file date */
            uint32_t y = parse_u(&q); if (*q == '-') q++;
            uint32_t m = parse_u(&q); if (*q == '-') q++;
            uint32_t d = parse_u(&q);
            if (y >= 1980 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
                cfg.fat_date = (uint16_t)(((y - 1980) << 9) | (m << 5) | d);
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    if (ads_osr_code(cfg.osr) < 0) cfg.osr = 2048;
    if (cfg.max_mb < 1) cfg.max_mb = 1;
    if (cfg.max_mb > 2000) cfg.max_mb = 2000;
}

/* ---------- shared state ---------- */
#define RING_N 256                     /* 128 KiB: ~1 s of SD stall at 125 kB/s */
static uint8_t ring[RING_N][LOG_BLOCK];
static volatile uint32_t ring_head, ring_tail, blk_seq;

enum { BUS_NONE, BUS_ADC, BUS_SD };
static volatile int bus_owner;
static volatile int adc_pending;
static volatile uint32_t pending_t, pending_sample;
static volatile uint32_t sample_no, last_drdy_t;
static volatile int acquiring;
static uint32_t period_us;

static volatile uint16_t cur_flags;
static uint8_t isr_blk[LOG_BLOCK];
static int isr_nrec;

static app_stats st;

/* shunt calibration averages: [start/end][off1, on, off2][ch] */
static int64_t cal_sum[2][3][ADS_NCH];
static uint32_t cal_n[2][3];
static volatile int cal_set = -1, cal_bucket = -1;
static volatile uint32_t cal_t0;

/* events */
#define EVQ_N 64
static uint8_t evq[EVQ_N][LOG_EVT_BYTES];
static volatile uint32_t evq_head, evq_tail;
static uint8_t evt_blk[LOG_BLOCK];
static int evt_n;
static uint32_t evt_t_last;

static void ev_push(uint16_t code, uint16_t a, uint32_t b) {
    uint32_t s = hal_irq_save();
    if (evq_head - evq_tail < EVQ_N) {
        uint8_t *e = evq[evq_head % EVQ_N];
        put_le32(e, hal_now_us()); put_le32(e + 4, sample_no);
        put_le16(e + 8, code); put_le16(e + 10, a); put_le32(e + 12, b);
        evq_head++;
    }
    hal_irq_restore(s);
}

/* commit a finished block to the ring; safe from main and ISR */
static int ring_commit(uint8_t *blk) {
    uint32_t s = hal_irq_save();
    int ok = ring_head - ring_tail < RING_N;
    if (ok) {
        put_le32(blk + 4, blk_seq++);
        u_memcpy(ring[ring_head % RING_N], blk, LOG_BLOCK);
        ring_head++;
        uint32_t used = ring_head - ring_tail;
        if (used > st.ring_max) st.ring_max = used;
    }
    hal_irq_restore(s);
    return ok;
}

/* ---------- acquisition (DRDY) ---------- */
static void acquire(uint32_t t, uint32_t sample, int late) {
    ads_frame f;
    imu_sample imu;
    uint16_t aux[AUX_N];

    spi1_config(SPI1_ADC);
    ads_read_frame(&f);
    imu_read(&imu);
    aux_read(aux);
    aux_trigger();

    uint16_t flags = cur_flags;
    if (!f.crc_ok) { flags |= LOGF_CRC_ERR; st.crc_err++; }
    if (late) { flags |= LOGF_LATE; st.late++; }
    if (!imu.fresh) flags |= LOGF_IMU_STALE;
    if (late && hal_now_us() - t >= period_us) { st.overrun++; ev_push(EVT_OVERRUN, 0, sample); }

    int b = cal_bucket;
    if (b >= 0 && t - cal_t0 > 100000 && f.crc_ok) {
        for (int i = 0; i < ADS_NCH; i++) cal_sum[cal_set][b][i] += f.ch[i];
        cal_n[cal_set][b]++;
    }

    if (isr_nrec == 0) { u_memset(isr_blk, 0, LOG_BLOCK); isr_blk[0] = 'D'; }
    uint8_t *r = isr_blk + LOG_BLOCK_HDR + isr_nrec * LOG_REC_BYTES;
    put_le32(r, sample); put_le32(r + 4, t);
    for (int i = 0; i < ADS_NCH; i++) {
        uint32_t v = (uint32_t)f.ch[i];
        r[8 + 3 * i] = (uint8_t)v; r[9 + 3 * i] = (uint8_t)(v >> 8); r[10 + 3 * i] = (uint8_t)(v >> 16);
    }
    put_le16(r + 32, f.status); put_le16(r + 34, flags);
    for (int i = 0; i < AUX_N; i++) put_le16(r + 36 + 2 * i, aux[i]);
    for (int i = 0; i < 3; i++) {
        put_le16(r + 46 + 2 * i, (uint16_t)imu.accel[i]);
        put_le16(r + 52 + 2 * i, (uint16_t)imu.gyro[i]);
    }
    put_le16(r + 58, (uint16_t)imu.temp);
    st.frames++;
    if (++isr_nrec == LOG_RECS_PER_BLOCK) {
        isr_blk[1] = (uint8_t)isr_nrec;
        if (!ring_commit(isr_blk)) {
            st.dropped += LOG_RECS_PER_BLOCK;
            ev_push(EVT_DROP, LOG_RECS_PER_BLOCK, sample);
        }
        isr_nrec = 0;
    }
}

void app_drdy_isr(void) {
    uint32_t t = hal_now_us();
    uint32_t n = sample_no++;
    last_drdy_t = t;
    if (!acquiring) return;
    if (bus_owner != BUS_NONE) {
        if (adc_pending) { st.overrun++; }
        adc_pending = 1; pending_t = t; pending_sample = n;
        return;
    }
    bus_owner = BUS_ADC;
    acquire(t, n, 0);
    bus_owner = BUS_NONE;
}

static int bus_take_sd(void) {
    uint32_t s = hal_irq_save();
    int ok = bus_owner == BUS_NONE && !adc_pending;
    if (ok) bus_owner = BUS_SD;
    hal_irq_restore(s);
    if (ok) spi1_config(SPI1_SD_FAST);
    return ok;
}

static void bus_release(void) {
    for (;;) {
        uint32_t s = hal_irq_save();
        if (!adc_pending) { bus_owner = BUS_NONE; hal_irq_restore(s); return; }
        bus_owner = BUS_ADC;
        adc_pending = 0;
        uint32_t t = pending_t, n = pending_sample;
        hal_irq_restore(s);
        acquire(t, n, 1);
    }
}

/* ---------- SD writer ---------- */
enum { WS_IDLE, WS_STREAM, WS_STOPPING, WS_DIR };
static struct {
    int state, card_busy;
    uint32_t written;                  /* blocks written to the file */
    fat_file file;
    int dir_due, closing, error;
    uint32_t cost[4];                  /* worst observed step time, us */
} w;
enum { C_POLL, C_CMD, C_BLOCK, C_DIRBLK };

#define ADC_SERVICE_US 60   /* worst-case DRDY work: ADC frame + IMU burst + record */

/* Start an SD step only if it ends before the next DRDY. When the ring is
 * filling up, a data block may instead run across one DRDY: that frame is
 * read as soon as the block ends (flagged LATE), still before the DRDY after. */
static int window_ok(int kind) {
    if (!acquiring) return 1;
    uint32_t since = hal_now_us() - last_drdy_t;
    uint32_t need = w.cost[kind] + 15;
    if (since + need < period_us) return 1;
    return kind == C_BLOCK && ring_head - ring_tail > RING_N / 4 &&
           since + need + ADC_SERVICE_US < 2 * period_us;
}

/* step-time estimate: jumps up to any slower step, relaxes 1 us per faster one */
static void note_cost(int kind, uint32_t t0) {
    uint32_t d = hal_now_us() - t0;
    if (d > w.cost[kind]) w.cost[kind] = d;
    else if (w.cost[kind] > d) w.cost[kind]--;
    if (kind == C_BLOCK && d > st.sd_block_us_max) st.sd_block_us_max = d;
}

/* one bounded SD step; returns 1 if it did something */
static int writer_step(void) {
    if (w.error) return 0;
    int have = ring_head != ring_tail;
    int kind;
    if (w.card_busy) kind = C_POLL;
    else if (w.state == WS_IDLE) {
        if (w.dir_due) kind = C_DIRBLK;
        else if (have && w.written < w.file.nblocks) kind = C_CMD;
        else return 0;
    } else if (w.state == WS_STREAM) {
        if (w.dir_due || (w.closing && !have) || w.written >= w.file.nblocks) kind = C_POLL;   /* stop token is tiny */
        else if (have) kind = C_BLOCK;
        else return 0;
    } else kind = C_POLL;
    if (!window_ok(kind) || !bus_take_sd()) return 0;

    uint32_t t0 = hal_now_us();
    int r = 0;
    if (w.card_busy) {
        r = sd_step_busy();
        if (r == 0) {
            w.card_busy = 0;
            if (w.state == WS_STOPPING || w.state == WS_DIR) w.state = WS_IDLE;
        }
        r = 0;
    } else if (w.state == WS_IDLE && w.dir_due) {
        fat_dir_with_size(&w.file, w.written * LOG_BLOCK);
        r = sd_step_cmd24(w.file.dir_lba, w.file.dir_sector);
        w.dir_due = 0; w.state = WS_DIR; w.card_busy = 1;
    } else if (w.state == WS_IDLE) {
        r = sd_step_cmd25(w.file.first_lba + w.written);
        w.state = WS_STREAM;
    } else if (kind == C_POLL) {
        r = sd_step_stop();
        w.state = WS_STOPPING; w.card_busy = 1;
    } else {
        r = sd_step_block(ring[ring_tail % RING_N]);
        if (r == 0) { ring_tail++; w.written++; }
        w.card_busy = 1;
    }
    note_cost(kind, t0);
    bus_release();
    if (r) {
        w.error = 1; st.sd_errors++; st.last_error = -20;
        ev_push(EVT_SD_ERROR, (uint16_t)kind, (uint32_t)r);
    }
    return 1;
}

/* ---------- run sequence ---------- */
static volatile int req_start, req_stop;
static int rstate = RS_IDLE, sub;
static uint32_t t_phase, t_last_dir, t_last_sync, t_last_status;
static uint32_t run_no;

void app_request_start(void) { req_start = 1; }
void app_request_stop(void) { req_stop = 1; }
const app_stats *app_get_stats(void) { st.state = rstate; return &st; }

static void set_flags(int phase) {
    uint16_t f = (uint16_t)(cur_flags & (LOGF_EXC | LOGF_SHUNT));
    cur_flags = (uint16_t)(f | (phase << LOGF_PHASE_SHIFT));
}
static void exc(int on) {
    gpio_exc_en(on);
    cur_flags = (uint16_t)(on ? cur_flags | LOGF_EXC : cur_flags & ~LOGF_EXC);
}
static void shunt(int on) {
    gpio_shunt(on);
    cur_flags = (uint16_t)(on ? cur_flags | LOGF_SHUNT : cur_flags & ~LOGF_SHUNT);
    ev_push(EVT_SHUNT, (uint16_t)on, 0);
}
static void enter(int s) {
    rstate = s; sub = 0; t_phase = hal_now_us();
    int phase = s == RS_SETTLE ? PHASE_SETTLE : s == RS_CAL_START ? PHASE_CAL_START
              : s == RS_RUN ? PHASE_RUN : s == RS_CAL_END ? PHASE_CAL_END : PHASE_IDLE;
    set_flags(phase);
    ev_push(EVT_PHASE, (uint16_t)phase, 0);
}

static void can_cmd(uint16_t id) {
    uint8_t d[8] = {cfg.node, (uint8_t)rstate, 0, 0, 0, 0, (uint8_t)run_no, (uint8_t)(run_no >> 8)};
    put_le32(d + 2, sample_no);
    can_send(id, d, 8);
}

static uint32_t hdr_len;
static uint8_t hdr_buf[LOG_HEADER_BYTES];
static void hdr(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    hdr_len += (uint32_t)u_vsnprintf((char *)hdr_buf + hdr_len, LOG_HEADER_BYTES - hdr_len, fmt, ap);
    va_end(ap);
    if (hdr_len >= LOG_HEADER_BYTES) hdr_len = LOG_HEADER_BYTES - 1;
}

static char run_name[13];        /* "RUN00001.BIN" */
static void make_run_name(void) {
    u_memcpy(run_name, w.file.name, 8);
    run_name[8] = '.';
    u_memcpy(run_name + 9, w.file.name + 8, 3);
    run_name[12] = 0;
}

static void build_header(void) {
    u_memset(hdr_buf, 0, sizeof hdr_buf);
    hdr_len = 0;
    uint32_t mrate = (uint32_t)(4000000000ull / cfg.osr);   /* milli-samples/s */
    hdr("SUSPLOG=1\nfirmware=" FW_VERSION "\n");
    hdr("board_uid=%08x-%08x-%08x\n", (unsigned)hal_uid(0), (unsigned)hal_uid(1), (unsigned)hal_uid(2));
    hdr("run_file=%s\nrun=%u\nnode=%u\ncan_master=%u\n", run_name, (unsigned)run_no, cfg.node, cfg.master);
    hdr("adc=ADS131M08\nadc_clk_hz=8000000\nosr=%u\nsample_rate_sps=%u.%03u\n",
        cfg.osr, (unsigned)(mrate / 1000), (unsigned)(mrate % 1000));
    hdr("gain=%u,%u,%u,%u,%u,%u,%u,%u\n", cfg.gain[0], cfg.gain[1], cfg.gain[2], cfg.gain[3],
        cfg.gain[4], cfg.gain[5], cfg.gain[6], cfg.gain[7]);
    hdr("adc_vref_v=1.2\nexc_nominal_v=2.49\nadc_filter=%s\n",
        cfg.osr >= 2048 ? "sinc3(1024)+sinc1(osr/1024)" : "sinc3(osr)");
    hdr("aux=AUX1,AUX2,AUX3,AUX4,VBAT\naux_bits=12\naux_vref_v=3.3\nvbat_divider=11\n");
    hdr("aux_lag_frames=1 (each record holds the MCU ADC scan started at the previous DRDY)\n");
    hdr("imu=ICM-42688-P\nimu_accel_lsb_per_g=2048\nimu_gyro_lsb_per_dps=16.4\nimu_odr_hz=2000\n");
    hdr("timestamp=us since boot, ADC and timer share one 8 MHz crystal\n");
    hdr("header_bytes=%u\nblock_bytes=%u\nrecord_bytes=%u\nrecords_per_block=%u\n",
        LOG_HEADER_BYTES, LOG_BLOCK, LOG_REC_BYTES, LOG_RECS_PER_BLOCK);
    hdr("cal_sequence=settle 1000ms; shunt off/on/off 500ms each at start and end; 100ms unexcited at end\n");
    hdr("[config]\n%s\n", cfg_text);
}

static void start_run(void) {
    st.last_error = 0;
    if (!gpio_sd_present()) { st.last_error = -1; con_printf("no card\n"); return; }
    if (sd_init()) { st.last_error = -2; con_printf("card init failed\n"); return; }
    if (fat_mount()) { st.last_error = -3; con_printf("card is not FAT32\n"); return; }
    cfg_defaults();
    int n = fat_read_file("CONFIG  TXT", (uint8_t *)cfg_text, CFG_MAX);
    cfg_text[n > 0 ? n : 0] = 0;
    cfg_parse();
    period_us = cfg.osr / 4u;
    if (ads_init(cfg.osr, cfg.gain)) { st.last_error = -4; con_printf("ADC init failed\n"); return; }

    u_memset(&w, 0, sizeof w);
    w.cost[C_POLL] = 10; w.cost[C_CMD] = 20; w.cost[C_BLOCK] = 190; w.cost[C_DIRBLK] = 200;
    if (fat_create_run(&w.file, cfg.max_mb * 1024u * 1024u, cfg.fat_date, 0)) {
        st.last_error = -5; con_printf("cannot create run file\n"); return;
    }
    run_no++;
    u_memset((void *)cal_sum, 0, sizeof cal_sum);
    u_memset((void *)cal_n, 0, sizeof cal_n);
    uint32_t s = hal_irq_save();
    ring_head = ring_tail = blk_seq = 0;
    evq_head = evq_tail = 0;
    hal_irq_restore(s);
    isr_nrec = 0; evt_n = 0;
    st.frames = st.dropped = st.crc_err = st.late = st.overrun = st.ring_max = st.sd_errors = 0;
    st.sd_block_us_max = 0;

    make_run_name();
    build_header();
    for (int i = 0; i < LOG_HEADER_BYTES / LOG_BLOCK; i++) {
        u_memcpy(ring[i], hdr_buf + i * LOG_BLOCK, LOG_BLOCK);
    }
    ring_head = LOG_HEADER_BYTES / LOG_BLOCK;

    uint32_t mrate = (uint32_t)(4000000000ull / cfg.osr);
    con_printf("run %u -> %s at %u.%03u sps, %u blocks preallocated\n", (unsigned)run_no, run_name,
               (unsigned)(mrate / 1000), (unsigned)(mrate % 1000), (unsigned)w.file.nblocks);
    cur_flags = 0;
    ev_push(EVT_RUN_START, (uint16_t)run_no, cfg.osr);
    adc_pending = 0; bus_owner = BUS_NONE;
    acquiring = 1;
    hal_drdy_irq_enable(1);
    exc(1);
    enter(RS_SETTLE);
    t_last_dir = t_last_sync = hal_now_us();
    if (cfg.master) can_cmd(0x081);
    st.runs++;
    led_log(1);
}

static void stop_acquisition(void) {
    hal_drdy_irq_enable(0);
    acquiring = 0;
    while (bus_owner != BUS_NONE || adc_pending) bus_release();
    if (isr_nrec) {
        isr_blk[1] = (uint8_t)isr_nrec;
        if (!ring_commit(isr_blk)) st.dropped += (uint32_t)isr_nrec;
        isr_nrec = 0;
    }
}

static int64_t cal_inc(int set, int ch) {
    if (!cal_n[set][0] || !cal_n[set][1] || !cal_n[set][2]) return 0;
    int64_t off1 = cal_sum[set][0][ch] / cal_n[set][0];
    int64_t on = cal_sum[set][1][ch] / cal_n[set][1];
    int64_t off2 = cal_sum[set][2][ch] / cal_n[set][2];
    return on - (off1 + off2) / 2;
}

/* code increment -> nV/V, nominal 2.49 V excitation, 1.2 V reference */
static int64_t counts_to_nvv(int64_t c, int ch) {
    return c * 481927711ll / ((int64_t)cfg.gain[ch] * 8388608ll);
}

static void flush_events(int force);

static void write_trailer(void) {
    static uint8_t blk[LOG_BLOCK];
    u_memset(blk, 0, sizeof blk);
    blk[0] = 'T';
    char *t = (char *)blk + LOG_BLOCK_HDR;
    int n = 0, cap = LOG_BLOCK - LOG_BLOCK_HDR;
    int drift_flag = 0;
    n += u_snprintf(t + n, (size_t)(cap - n), "frames=%u\ndropped=%u\ncrc_err=%u\nlate=%u\noverrun=%u\n",
                    (unsigned)st.frames, (unsigned)st.dropped, (unsigned)st.crc_err, (unsigned)st.late,
                    (unsigned)st.overrun);
    n += u_snprintf(t + n, (size_t)(cap - n), "ring_max=%u\nsd_block_us_max=%u\n", (unsigned)st.ring_max,
                    (unsigned)st.sd_block_us_max);
    n += u_snprintf(t + n, (size_t)(cap - n), "shunt_nvv_start,shunt_nvv_end,drift_ppm\n");
    for (int ch = 0; ch < ADS_NCH; ch++) {
        int64_t a = counts_to_nvv(cal_inc(0, ch), ch), b = counts_to_nvv(cal_inc(1, ch), ch);
        int64_t ppm = a ? (b - a) * 1000000 / a : 0;
        if (ppm > 5000 || ppm < -5000) drift_flag = 1;
        n += u_snprintf(t + n, (size_t)(cap - n), "ch%d=%lld,%lld,%lld\n", ch + 1, (long long)a, (long long)b,
                        (long long)ppm);
    }
    u_snprintf(t + n, (size_t)(cap - n), "gain_drift_flag=%d\n", drift_flag);
    ring_commit(blk);
}

static void events_to_blocks(void) {
    while (evq_tail != evq_head) {
        if (evt_n == 0) { u_memset(evt_blk, 0, LOG_BLOCK); evt_blk[0] = 'E'; }
        u_memcpy(evt_blk + LOG_BLOCK_HDR + evt_n * LOG_EVT_BYTES, evq[evq_tail % EVQ_N], LOG_EVT_BYTES);
        evq_tail++;
        if (++evt_n == LOG_EVTS_PER_BLOCK) flush_events(1);
    }
}

static void flush_events(int force) {
    if (!evt_n || (!force && hal_now_us() - evt_t_last < 1000000)) return;
    evt_blk[1] = (uint8_t)evt_n;
    ring_commit(evt_blk);
    evt_n = 0;
    evt_t_last = hal_now_us();
}

static void abort_run(int err) {
    stop_acquisition();
    exc(0); shunt(0);
    st.last_error = err;
    rstate = RS_IDLE;
    led_log(0);
    con_printf("run aborted (%d)\n", err);
}

static void run_poll(void) {
    uint32_t now = hal_now_us(), dt = now - t_phase;

    if (rstate != RS_IDLE && rstate != RS_CLOSING) {
        if (!gpio_sd_present()) { abort_run(-10); return; }
        if (w.error) { abort_run(-20); return; }
        if (now - t_last_dir > 2000000) { w.dir_due = 1; t_last_dir = now; }
        if (cfg.master && now - t_last_sync > 100000) {
            t_last_sync = now;
            can_cmd(0x080);
            ev_push(EVT_CAN_SYNC_TX, cfg.node, sample_no);
        }
        /* keep room for the end-of-run calibration (about 4 s) */
        uint32_t reserve = 4u * 1000000u / period_us / LOG_RECS_PER_BLOCK + 64;
        if (rstate == RS_RUN && w.written + (ring_head - ring_tail) + reserve > w.file.nblocks) req_stop = 1;
    }

    switch (rstate) {
    case RS_IDLE:
        if (req_start) { req_start = req_stop = 0; start_run(); }
        req_stop = 0;
        break;
    case RS_SETTLE:
        if (dt >= 1000000) { enter(RS_CAL_START); cal_set = 0; cal_bucket = 0; cal_t0 = now; }
        break;
    case RS_CAL_START:
    case RS_CAL_END:
        if (dt >= 500000u * (uint32_t)(sub + 1)) {
            sub++;
            cal_t0 = now;
            if (sub == 1) { shunt(1); cal_bucket = 1; }
            else if (sub == 2) { shunt(0); cal_bucket = 2; }
            else {
                cal_bucket = -1;
                if (rstate == RS_CAL_START) enter(RS_RUN);
                else exc(0);    /* keep logging 100 ms unexcited: zero-excitation offset record */
            }
        }
        if (rstate == RS_CAL_END && sub == 3 && dt >= 1600000) {
            stop_acquisition();
            ev_push(EVT_RUN_STOP, (uint16_t)run_no, st.frames);
            events_to_blocks();
            flush_events(1);
            write_trailer();
            w.closing = 1;
            rstate = RS_CLOSING;
        }
        break;
    case RS_RUN:
        if (req_stop) {
            req_stop = 0;
            if (cfg.master) can_cmd(0x082);
            enter(RS_CAL_END); cal_set = 1; cal_bucket = 0; cal_t0 = now;
        }
        break;
    case RS_CLOSING:
        if (w.error) { abort_run(-20); break; }
        if (w.written >= w.file.nblocks && w.state == WS_IDLE) {   /* file full: discard the rest */
            st.dropped += ring_head - ring_tail;
            ring_tail = ring_head;
        }
        if (ring_head == ring_tail && w.state == WS_IDLE && !w.card_busy) {
            int r = fat_close(&w.file, w.written * LOG_BLOCK);
            rstate = RS_IDLE;
            led_log(0);
            if (r) st.last_error = -21;
            con_printf("run %u closed: %u frames, %u dropped, %u bytes\n", (unsigned)run_no,
                       (unsigned)st.frames, (unsigned)st.dropped, (unsigned)(w.written * LOG_BLOCK));
        }
        break;
    }
}

/* ---------- CAN ---------- */
#define CANQ_N 16
static struct { uint16_t id; uint8_t len; uint8_t d[8]; uint32_t t; } canq[CANQ_N];
static volatile uint32_t canq_head, canq_tail;

void app_can_rx(uint16_t id, const uint8_t *data, uint8_t len, uint32_t t_us) {
    if (canq_head - canq_tail >= CANQ_N) return;
    canq[canq_head % CANQ_N].id = id;
    canq[canq_head % CANQ_N].len = len;
    canq[canq_head % CANQ_N].t = t_us;
    u_memcpy(canq[canq_head % CANQ_N].d, data, len > 8 ? 8 : len);
    canq_head++;
}

static void can_poll(void) {
    while (canq_tail != canq_head) {
        uint16_t id = canq[canq_tail % CANQ_N].id;
        const uint8_t *d = canq[canq_tail % CANQ_N].d;
        if (d[0] != cfg.node) {
            if (id == 0x080) ev_push(EVT_CAN_SYNC_RX, d[0], get_le32(d + 2));
            else if (id == 0x081) { ev_push(EVT_CAN_CMD_RX, d[0], 1); req_start = 1; }
            else if (id == 0x082) { ev_push(EVT_CAN_CMD_RX, d[0], 0); req_stop = 1; }
        }
        canq_tail++;
    }
    uint32_t now = hal_now_us();
    if (now - t_last_status > 1000000) {
        t_last_status = now;
        uint8_t d[8] = {cfg.node, (uint8_t)rstate, (uint8_t)st.last_error, 0, 0, 0,
                        (uint8_t)((ring_head - ring_tail) * 100 / RING_N), 0};
        put_le16(d + 3, (uint16_t)(st.dropped > 0xFFFF ? 0xFFFF : st.dropped));
        can_send((uint16_t)(0x100 + cfg.node), d, 8);
    }
}

/* ---------- console ---------- */
static char line[48];
static int line_n;

static void console_poll(void) {
    int c;
    while ((c = uart_getc()) >= 0) {
        if (c != '\r' && c != '\n') { if (line_n < (int)sizeof line - 1) line[line_n++] = (char)c; continue; }
        line[line_n] = 0;
        if (!u_strncmp(line, "start", 6)) app_request_start();
        else if (!u_strncmp(line, "stop", 5)) app_request_stop();
        else if (!u_strncmp(line, "status", 7))
            con_printf("state=%d frames=%u dropped=%u late=%u crc=%u ring=%u/%u err=%d card=%d\n", rstate,
                       (unsigned)st.frames, (unsigned)st.dropped, (unsigned)st.late, (unsigned)st.crc_err,
                       (unsigned)(ring_head - ring_tail), RING_N, st.last_error, gpio_sd_present());
        else if (line_n) con_printf("commands: start stop status\n");
        line_n = 0;
    }
}

/* ---------- top level ---------- */
static int can_ok;
static uint32_t t_boot;

void app_init(void) {
    cfg_defaults();
    gpio_exc_en(0); gpio_shunt(0); led_log(0); led_err(0);
    if (imu_init()) { st.last_error = -6; con_printf("IMU not found\n"); }
    can_ok = can_init();
    t_boot = hal_now_us();
    con_printf("suspension logger " FW_VERSION ", CAN %s\n", can_ok ? "up" : "off (no vehicle power)");
    if (gpio_sd_present() && !sd_init() && !fat_mount()) {
        int n = fat_read_file("CONFIG  TXT", (uint8_t *)cfg_text, CFG_MAX);
        cfg_text[n > 0 ? n : 0] = 0;
        cfg_parse();
    }
}

void app_poll(void) {
    console_poll();
    if (can_ok) can_poll();
    if (cfg.autostart == 1 && rstate == RS_IDLE && st.runs == 0 && gpio_sd_present() &&
        hal_now_us() - t_boot > 3000000) {
        cfg.autostart = 2;                 /* once per boot */
        req_start = 1;
    }
    run_poll();
    if (rstate != RS_IDLE) {
        events_to_blocks();
        if (rstate != RS_CLOSING) flush_events(0);
        while (writer_step() && !acquiring) { }
    }
    uint32_t ms = hal_now_us() / 1000;
    led_err(st.last_error != 0 ? 1 : !gpio_sd_present() ? (ms / 125) & 1 : 0);
    if (rstate == RS_IDLE) led_log((ms % 1000) < 50);
}
