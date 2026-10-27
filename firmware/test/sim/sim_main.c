/* Runs the real firmware (src/) against the simulated board for one run:
 * boot, "start" on the console, scenario playback, "stop", file close.
 *
 *   sim <scenario.csv> <card.img> <report.json> [--stall-at-block N --stall-ms M]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app.h"
#include "../../src/hal.h"
#include "sim.h"

extern uint32_t bus_conflicts, can_tx_sync, can_tx_start, can_tx_stop, can_tx_status;
extern FILE *uart_log;
void sim_uart_inject(const char *s);
uint64_t sim_drdy_phase_ns(void);

/* ---- scenario playback ---- */
static double *rows; static int nrows, ncols; static double dt_s;
static int col[SC_N];
double sim_drive_start_s = 4.0;

int scenario_load(const char *path) {
    static const char *names[SC_N] = {"ch1_mvv", "ch2_mvv", "ch3_mvv", "ch4_mvv", "ch5_mvv", "ch6_mvv", "ch7_mvv",
                                      "ch8_mvv", "aux1_v", "accel_x_g", "accel_y_g", "accel_z_g", "gyro_x_dps",
                                      "gyro_y_dps", "gyro_z_dps"};
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[4096];
    if (!fgets(line, sizeof line, f)) return -2;
    char *hdr[64]; ncols = 0;
    for (char *tok = strtok(line, ",\r\n"); tok && ncols < 64; tok = strtok(NULL, ",\r\n")) hdr[ncols++] = strdup(tok);
    for (int k = 0; k < SC_N; k++) {
        col[k] = -1;
        for (int c = 0; c < ncols; c++) if (!strcmp(hdr[c], names[k])) col[k] = c;
        if (col[k] < 0) { fprintf(stderr, "scenario missing column %s\n", names[k]); return -3; }
    }
    int cap = 1 << 16;
    rows = malloc(sizeof(double) * (size_t)cap * (size_t)ncols);
    while (fgets(line, sizeof line, f)) {
        if (nrows == cap) { cap *= 2; rows = realloc(rows, sizeof(double) * (size_t)cap * (size_t)ncols); }
        char *p = line;
        for (int c = 0; c < ncols; c++) { rows[(size_t)nrows * ncols + c] = strtod(p, &p); if (*p == ',') p++; }
        nrows++;
    }
    fclose(f);
    dt_s = rows[ncols] - rows[0];
    return nrows > 1 ? 0 : -4;
}

void scenario_at(double t, double out[SC_N]) {
    double x = (t - rows[0]) / dt_s;
    if (x < 0) x = 0;
    if (x > nrows - 1) x = nrows - 1;
    int i = (int)x; if (i >= nrows - 1) i = nrows - 2;
    double a = x - i;
    for (int k = 0; k < SC_N; k++) {
        double v0 = rows[(size_t)i * ncols + col[k]], v1 = rows[(size_t)(i + 1) * ncols + col[k]];
        out[k] = v0 + a * (v1 - v0);
    }
}

int sim_logging(void) { int st = app_get_stats()->state; return st != RS_IDLE && st != RS_CLOSING; }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: sim scenario.csv card.img report.json [--stall-at-block N --stall-ms M]\n"); return 2; }
    for (int i = 4; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--stall-at-block")) sd_big_stall_at_block = (uint32_t)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--stall-ms")) sd_big_stall_us = (uint32_t)atoi(argv[i + 1]) * 1000u;
    }
    if (scenario_load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }
    fseek(f, 0, SEEK_END); uint64_t sz = (uint64_t)ftell(f); fclose(f);
    if (sd_dev_open(argv[2], sz)) return 1;

    char logpath[1024];
    snprintf(logpath, sizeof logpath, "%s.console.txt", argv[3]);
    uart_log = fopen(logpath, "w");

    double drive_len = rows[(size_t)(nrows - 1) * ncols] - rows[0];
    double t_start = 0.2, t_stop = sim_drive_start_s + drive_len + 0.5;
    int started = 0, stopped = 0, sync_injected = 0;

    app_init();
    while (1) {
        double t = sim_t_ns * 1e-9;
        if (!started && t >= t_start) { sim_uart_inject("start\n"); started = 1; }
        if (!sync_injected && t >= 6.0) {             /* another corner board's SYNC */
            uint8_t d[8] = {2, RS_RUN, 0x10, 0x27, 0, 0, 1, 0};
            app_can_rx(0x080, d, 8, hal_now_us());
            sync_injected = 1;
        }
        if (!stopped && t >= t_stop) { sim_uart_inject("stop\n"); stopped = 1; }
        const app_stats *s = app_get_stats();
        if (stopped && s->state == RS_IDLE && t > t_stop + 3.5) break;
        if (t > t_stop + 30) { fprintf(stderr, "timeout waiting for the run to close\n"); break; }
        app_poll();
        sim_advance_ns(2000);
    }
    fclose(uart_log);
    sd_dev_save(argv[2]);

    const app_stats *s = app_get_stats();
    FILE *r = fopen(argv[3], "w");
    fprintf(r, "{\n");
    fprintf(r, "  \"drive_start_s\": %.6f,\n  \"exc_volts\": %.6f,\n", sim_drive_start_s, sim_exc_volts);
    fprintf(r, "  \"drdy_phase_ns\": %llu,\n  \"drdy_period_ns\": %llu,\n", (unsigned long long)sim_drdy_phase_ns(),
            (unsigned long long)ads_dev_period_ns());
    fprintf(r, "  \"fw\": {\"frames\": %u, \"dropped\": %u, \"crc_err\": %u, \"late\": %u, \"overrun\": %u, "
               "\"ring_max\": %u, \"sd_errors\": %u, \"sd_block_us_max\": %u, \"runs\": %u, \"last_error\": %d},\n",
            s->frames, s->dropped, s->crc_err, s->late, s->overrun, s->ring_max, s->sd_errors, s->sd_block_us_max,
            s->runs, s->last_error);
    fprintf(r, "  \"devices\": {\"bus_conflicts\": %u, \"ads_wrong_spi_mode\": %u, \"ads_bad_frames\": %u, "
               "\"ads_conversions_missed\": %u, \"sd_protocol_errors\": %u, \"sd_wrong_spi_mode\": %u, "
               "\"sd_blocks_written\": %u, \"sd_long_stalls\": %u, \"imu_errors\": %u},\n",
            bus_conflicts, ads_err_mode, ads_err_frame, ads_missed, sd_err_proto, sd_err_mode, sd_blocks_written,
            sd_long_stalls, imu_err);
    fprintf(r, "  \"can_tx\": {\"sync\": %u, \"start\": %u, \"stop\": %u, \"status\": %u},\n", can_tx_sync,
            can_tx_start, can_tx_stop, can_tx_status);
    fprintf(r, "  \"sim_end_s\": %.3f\n}\n", sim_t_ns * 1e-9);
    fclose(r);
    printf("sim done at t=%.2f s: %u frames, %u dropped, %u late reads, ring max %u/256\n", sim_t_ns * 1e-9,
           s->frames, s->dropped, s->late, s->ring_max);
    return 0;
}
