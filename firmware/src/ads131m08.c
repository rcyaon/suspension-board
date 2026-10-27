/* TI ADS131M08 driver (SBAS950B). SPI mode 1, 24-bit words, 10-word frames.
 * The ADC clock is MCO1 (8 MHz HSE), which must already be running. */
#include "ads131m08.h"
#include "hal.h"
#include "util.h"

#define CMD_NULL   0x0000
#define CMD_RREG(a)  (0xA000 | ((a) << 7))
#define CMD_WREG(a)  (0x6000 | ((a) << 7))
#define REG_ID     0x00
#define REG_MODE   0x02
#define REG_CLOCK  0x03
#define REG_GAIN1  0x04
#define REG_GAIN2  0x05

/* MODE: 24-bit words, SPI timeout on, DRDY active-low level, clear RESET flag */
#define MODE_VALUE 0x0110

uint16_t ads_crc16(const uint8_t *p, uint32_t n) {   /* CCITT, init 0xFFFF */
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

int ads_osr_code(uint16_t osr) {
    static const uint16_t tab[8] = {128, 256, 512, 1024, 2048, 4096, 8192, 16256};
    for (int i = 0; i < 8; i++) if (tab[i] == osr) return i;
    return -1;
}

static int gain_code(uint8_t g) {
    for (int i = 0; i < 8; i++) if ((1u << i) == g) return i;
    return -1;
}

/* one full frame: word0 = cmd, word1 = optional data, rest zero */
static void frame(uint16_t cmd, int has_data, uint16_t data, uint8_t rx[ADS_FRAME_BYTES]) {
    uint8_t tx[ADS_FRAME_BYTES];
    u_memset(tx, 0, sizeof tx);
    tx[0] = (uint8_t)(cmd >> 8); tx[1] = (uint8_t)cmd;
    if (has_data) { tx[3] = (uint8_t)(data >> 8); tx[4] = (uint8_t)data; }
    adc_cs(1);
    spi1_xfer_buf(tx, rx, ADS_FRAME_BYTES);
    adc_cs(0);
}

static uint16_t rreg(uint8_t addr) {
    uint8_t rx[ADS_FRAME_BYTES];
    frame(CMD_RREG(addr), 0, 0, rx);
    frame(CMD_NULL, 0, 0, rx);                /* response arrives in the next frame */
    return (uint16_t)((rx[0] << 8) | rx[1]);
}

static void wreg(uint8_t addr, uint16_t v) {
    uint8_t rx[ADS_FRAME_BYTES];
    frame(CMD_WREG(addr), 1, v, rx);
    frame(CMD_NULL, 0, 0, rx);
}

int ads_init(uint16_t osr, const uint8_t gain[ADS_NCH]) {
    int oc = ads_osr_code(osr);
    if (oc < 0) return -1;
    uint16_t g1 = 0, g2 = 0;
    for (int i = 0; i < 4; i++) {
        int a = gain_code(gain[i]), b = gain_code(gain[i + 4]);
        if (a < 0 || b < 0) return -2;
        g1 |= (uint16_t)(a << (4 * i));
        g2 |= (uint16_t)(b << (4 * i));
    }

    /* hardware reset: SYNC/RESET low for >= 2048 CLKIN periods (256 us) with the
     * clock running, then >= 5 us before the first SPI access. */
    gpio_adc_reset(1);
    hal_delay_us(1000);
    gpio_adc_reset(0);
    hal_delay_us(100);

    spi1_config(SPI1_ADC);
    uint16_t id = rreg(REG_ID);
    if ((id & 0xFF00) != 0x2800) return -3;

    wreg(REG_MODE, MODE_VALUE);
    wreg(REG_CLOCK, (uint16_t)(0xFF00 | (oc << 2) | 0x2));   /* all channels, high-resolution */
    wreg(REG_GAIN1, g1);
    wreg(REG_GAIN2, g2);

    if (rreg(REG_MODE) != MODE_VALUE) return -4;
    if (rreg(REG_CLOCK) != (uint16_t)(0xFF00 | (oc << 2) | 0x2)) return -4;
    if (rreg(REG_GAIN1) != g1 || rreg(REG_GAIN2) != g2) return -4;
    return 0;
}

void ads_read_frame(ads_frame *f) {
    uint8_t rx[ADS_FRAME_BYTES];
    frame(CMD_NULL, 0, 0, rx);
    f->status = (uint16_t)((rx[0] << 8) | rx[1]);
    for (int i = 0; i < ADS_NCH; i++) {
        const uint8_t *w = &rx[(i + 1) * ADS_WORD];
        int32_t v = (int32_t)(((uint32_t)w[0] << 24) | ((uint32_t)w[1] << 16) | ((uint32_t)w[2] << 8)) >> 8;
        f->ch[i] = v;
    }
    uint16_t crc = (uint16_t)((rx[27] << 8) | rx[28]);
    f->crc_ok = crc == ads_crc16(rx, 27);
}
