/* ADS131M08 model: register file, SPI frame protocol with CRC, reset timing,
 * and conversions of the scenario's bridge signals through the board's
 * excitation, shunt FETs, PGA, internal 1.2 V reference and input noise. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/hal.h"
#include "sim.h"

uint32_t ads_err_mode, ads_err_frame, ads_missed;
double sim_exc_volts = 2.4870;        /* within the 2.43-2.55 V acceptance window, not exactly 2.49 */

static uint16_t reg[64];
static int sel, nrx, in_reset = 1, configured;
static uint8_t rx[64], tx[30];
static uint16_t resp;
static int32_t data[8];
static int unread;
static uint64_t reset_low_ns, exc_on_ns;
static int last_exc;

static const double ch_offset_uv[8] = {3.1, -2.4, 1.7, 0.6, -4.2, 2.2, -0.9, 1.3};
static const double noise_uv = 0.35;  /* input-referred rms, typical at PGA 128 */
#define R_GAUGE 350.0
#define R_SHUNT 100000.0

static uint32_t rng = 12345u;
static double gauss(void) {
    double u1, u2;
    rng = rng * 1664525u + 1013904223u; u1 = (rng >> 8) / 16777216.0 + 1e-12;
    rng = rng * 1664525u + 1013904223u; u2 = (rng >> 8) / 16777216.0;
    return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

static uint16_t crc16(const uint8_t *p, int n) {
    uint16_t c = 0xFFFF;
    while (n--) { c ^= (uint16_t)(*p++ << 8); for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); }
    return c;
}

static void defaults(void) {
    memset(reg, 0, sizeof reg);
    reg[0x00] = 0x2811; reg[0x01] = 0x0500; reg[0x02] = 0x0510; reg[0x03] = 0xFF0E;
    resp = 0xFF28;
}

void ads_dev_reset_pin(int low) {
    if (low) { reset_low_ns = sim_t_ns; in_reset = 1; return; }
    /* needs >= 2048 CLKIN periods (256 us at 8 MHz) low with the clock running */
    if (sim_t_ns - reset_low_ns >= 256000) { defaults(); configured = 1; }
    else ads_err_frame++;
    in_reset = 0;
}

int ads_dev_converting(void) { return configured && !in_reset; }
uint64_t ads_dev_period_ns(void) {
    static const int osr[8] = {128, 256, 512, 1024, 2048, 4096, 8192, 16256};
    return (uint64_t)osr[(reg[3] >> 2) & 7] * 250;   /* fMOD = 4 MHz */
}

static double excitation(uint64_t t) {
    if (pin_exc && !last_exc) exc_on_ns = t;
    last_exc = pin_exc;
    if (!pin_exc) return 0.0;
    return sim_exc_volts * (1.0 - exp(-(double)(t - exc_on_ns) / 20e6));   /* LT3042 + bulk cap */
}

void ads_dev_convert(uint64_t t) {
    /* count conversions overwritten unread, only while the firmware is logging */
    static int prev_counting;
    int counting = sim_logging();
    if (unread && counting && prev_counting) {
        ads_missed++;
        if (getenv("SIM_DEBUG")) fprintf(stderr, "missed conversion at %.6f s\n", t * 1e-9);
    }
    prev_counting = counting;
    double sc[SC_N];
    scenario_at(t * 1e-9 - sim_drive_start_s, sc);
    double vexc = excitation(t);
    double shunt_vv = pin_shunt ? -R_GAUGE / (4 * R_SHUNT + 2 * R_GAUGE) : 0.0;
    for (int i = 0; i < 8; i++) {
        int gcode = (reg[4 + i / 4] >> (4 * (i % 4))) & 7;
        double gain = (double)(1 << gcode);
        double vin = (sc[SC_CH1 + i] * 1e-3 + shunt_vv) * vexc + (ch_offset_uv[i] + noise_uv * gauss()) * 1e-6;
        double code = vin * gain / 1.2 * 8388608.0;
        code = code > 8388607 ? 8388607 : code < -8388608 ? -8388608 : code;
        data[i] = (int32_t)lround(code);
    }
    unread = 1;
}

static void put24(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }

void ads_dev_cs(int s) {
    if (s) {
        sel = 1; nrx = 0;
        put24(tx, (uint32_t)resp << 8);
        for (int i = 0; i < 8; i++) put24(tx + 3 + 3 * i, (uint32_t)data[i] & 0xFFFFFF);
        put24(tx + 27, (uint32_t)crc16(tx, 27) << 8);
        unread = 0;
        return;
    }
    if (!sel) return;
    sel = 0;
    if (nrx != 30) { ads_err_frame++; return; }
    uint16_t cmd = (uint16_t)((rx[0] << 8) | rx[1]);
    uint16_t dat = (uint16_t)((rx[3] << 8) | rx[4]);
    if (cmd == 0x0000) resp = reg[1];
    else if (cmd == 0x0011) { defaults(); }
    else if ((cmd & 0xE000) == 0xA000) resp = reg[(cmd >> 7) & 0x3F];
    else if ((cmd & 0xE000) == 0x6000) {
        int a = (cmd >> 7) & 0x3F;
        if (a == 0x02) dat &= (uint16_t)~0x0400;   /* RESET flag clears when written 0 */
        reg[a] = dat;
        resp = (uint16_t)(0x4000 | (a << 7));
    } else ads_err_frame++;
}

uint8_t ads_dev_byte(uint8_t mosi) {
    if (sim_spi1_mode() != SPI1_ADC) ads_err_mode++;
    if (in_reset) ads_err_frame++;
    uint8_t out = nrx < 30 ? tx[nrx] : 0;
    if (nrx < (int)sizeof rx) rx[nrx] = mosi;
    nrx++;
    return out;
}
