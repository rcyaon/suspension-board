/* ICM-42688-P model: registers, 2 kHz output data rate, latched data-ready
 * flag cleared by reading INT_STATUS, big-endian sensor registers. */
#include <math.h>
#include <string.h>
#include "sim.h"

uint32_t imu_err;
static uint8_t reg[128];
static int sel, n, rd, addr;
static uint64_t last_tick_read;

static int16_t sat16(double v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : lround(v)); }

static void snapshot(void) {
    uint64_t tick = sim_t_ns / 500000;       /* 2 kHz ODR */
    double sc[SC_N];
    scenario_at(tick * 500e-6 - sim_drive_start_s, sc);
    int16_t v[7] = {sat16(5.0 * 132.48),     /* 30 C */
                    sat16(sc[SC_AX] * 2048), sat16(sc[SC_AY] * 2048), sat16(sc[SC_AZ] * 2048),
                    sat16(sc[SC_GX] * 16.4), sat16(sc[SC_GY] * 16.4), sat16(sc[SC_GZ] * 16.4)};
    if (reg[0x4E] != 0x0F) memset(v, 0, sizeof v);
    for (int i = 0; i < 7; i++) { reg[0x1D + 2 * i] = (uint8_t)(v[i] >> 8); reg[0x1E + 2 * i] = (uint8_t)v[i]; }
    reg[0x2D] = tick != last_tick_read ? 0x08 : 0x00;
    last_tick_read = tick;
}

void imu_dev_cs(int s) {
    sel = s; n = 0;
    if (s) return;
}

uint8_t imu_dev_byte(uint8_t b) {
    if (!sel) { imu_err++; return 0xFF; }
    if (n++ == 0) {
        rd = (b & 0x80) != 0; addr = b & 0x7F;
        if (rd && addr == 0x1D) snapshot();
        return 0xFF;
    }
    uint8_t out = 0;
    if (rd) {
        out = addr == 0x75 ? 0x47 : reg[addr];
        if (addr == 0x2D) reg[0x2D] = 0;
    } else {
        if (addr == 0x11 && (b & 1)) memset(reg, 0, sizeof reg);
        else reg[addr] = b;
    }
    addr = (addr + 1) & 0x7F;
    return out;
}
