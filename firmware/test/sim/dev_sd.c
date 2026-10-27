/* microSD (SDHC) in SPI mode over a disk image. Models command/response
 * timing, the CS-high byte needed to release MISO, write-busy time after
 * each block (with periodic long garbage-collection stalls), and checks
 * that the host used the right SPI mode and the slow clock during init. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/hal.h"
#include "sim.h"

int sd_dev_driving;
uint32_t sd_err_proto, sd_err_mode, sd_blocks_written, sd_long_stalls;
uint32_t sd_stall_every_blocks = 2000, sd_stall_us = 120000;    /* ~every 8 s at 244 blocks/s */
uint32_t sd_big_stall_at_block, sd_big_stall_us;

static uint8_t *img;
static uint64_t img_bytes;
static int sel, ready, app_cmd, acmd41_count;
enum { S_IDLE, S_CMD, S_WAIT_TOKEN, S_DATA };
static int state, multi;
static uint8_t cmdb[6]; static int ncmd;
static uint8_t outq[600]; static int out_h, out_t;
static uint8_t blk[514]; static int nblk;
static uint32_t wr_lba;
static uint64_t busy_until;
static uint32_t rng = 777u;

int sd_dev_open(const char *path, uint64_t bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    img = calloc(1, bytes); img_bytes = bytes;
    size_t n = fread(img, 1, bytes, f);
    fclose(f);
    return n == bytes ? 0 : -2;
}
int sd_dev_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(img, 1, img_bytes, f);
    fclose(f);
    return 0;
}

static void q(uint8_t b) { if (out_h < (int)sizeof outq) outq[out_h++] = b; }

static void exec_cmd(void) {
    int idx = cmdb[0] & 0x3F;
    uint32_t arg = ((uint32_t)cmdb[1] << 24) | ((uint32_t)cmdb[2] << 16) | ((uint32_t)cmdb[3] << 8) | cmdb[4];
    int acmd = app_cmd; app_cmd = 0;
    uint8_t r1 = ready ? 0x00 : 0x01;
    q(0xFF);                                  /* NCR: one byte before the response */
    state = S_IDLE;
    if (idx == 0) { ready = 0; acmd41_count = 0; q(0x01); }
    else if (idx == 8) { q(r1); q(0); q(0); q(0x01); q((uint8_t)arg); }
    else if (idx == 55) { app_cmd = 1; q(r1); }
    else if (idx == 41 && acmd) { if (++acmd41_count >= 4) ready = 1; q(ready ? 0x00 : 0x01); }
    else if (idx == 58) { q(r1); q(0xC0); q(0xFF); q(0x80); q(0x00); }
    else if (!ready) { q(0x05); sd_err_proto++; }
    else if (idx == 17) {
        if ((uint64_t)arg * 512 + 512 > img_bytes) { q(0x40); sd_err_proto++; return; }
        q(0x00); q(0xFF); q(0xFE);
        for (int i = 0; i < 512; i++) q(img[(uint64_t)arg * 512 + i]);
        q(0xFF); q(0xFF);
    } else if (idx == 24 || idx == 25) {
        q(0x00); wr_lba = arg; multi = idx == 25; state = S_WAIT_TOKEN;
    } else if (idx == 16) q(0x00);
    else { q(0x04); sd_err_proto++; }
}

static uint64_t write_latency_ns(void) {
    sd_blocks_written++;
    if (sd_big_stall_at_block && sd_blocks_written == sd_big_stall_at_block) { sd_long_stalls++; return (uint64_t)sd_big_stall_us * 1000; }
    if (sd_stall_every_blocks && sd_blocks_written % sd_stall_every_blocks == 0) { sd_long_stalls++; return (uint64_t)sd_stall_us * 1000; }
    rng = rng * 1664525u + 1013904223u;
    return 150000 + (rng >> 8) % 450000;       /* 150-600 us programming time */
}

void sd_dev_cs(int s) {
    if (s) { sel = 1; sd_dev_driving = 1; return; }
    sel = 0;
    out_h = out_t = 0;
    if (state == S_CMD) state = S_IDLE;
}

uint8_t sd_dev_byte(uint8_t b) {
    int m = sim_spi1_mode();
    if (m == SPI1_ADC) sd_err_mode++;
    if (!ready && m != SPI1_SD_SLOW) sd_err_mode++;      /* init must run at <= 400 kHz */

    uint8_t out;
    if (out_t < out_h) out = outq[out_t++];
    else if (sim_t_ns < busy_until) out = 0x00;
    else out = 0xFF;
    if (out_t == out_h) out_h = out_t = 0;

    if (sim_t_ns < busy_until) { if (b != 0xFF) sd_err_proto++; return out; }
    switch (state) {
    case S_IDLE:
        if ((b & 0xC0) == 0x40) { cmdb[0] = b; ncmd = 1; state = S_CMD; }
        else if (b != 0xFF) sd_err_proto++;
        break;
    case S_CMD:
        cmdb[ncmd++] = b;
        if (ncmd == 6) exec_cmd();
        break;
    case S_WAIT_TOKEN:
        if ((!multi && b == 0xFE) || (multi && b == 0xFC)) { state = S_DATA; nblk = 0; }
        else if (multi && b == 0xFD) { state = S_IDLE; busy_until = sim_t_ns + 2000 + 300000; }
        else if (b != 0xFF) { sd_err_proto++; state = S_IDLE; }
        break;
    case S_DATA:
        blk[nblk++] = b;
        if (nblk == 514) {
            if ((uint64_t)wr_lba * 512 + 512 <= img_bytes) memcpy(img + (uint64_t)wr_lba * 512, blk, 512);
            else sd_err_proto++;
            wr_lba++;
            q(0xE5);                                     /* data accepted (xxx00101) */
            busy_until = sim_t_ns + 700 + write_latency_ns();
            state = multi ? S_WAIT_TOKEN : S_IDLE;
        }
        break;
    }
    return out;
}
