/* microSD in SPI mode on the shared SPI1 bus. The card shares SCK/MOSI/MISO
 * with the ADS131M08; after every transaction CS goes high and one extra byte
 * is clocked so the card releases MISO before the ADC is selected. */
#include "sd_spi.h"
#include "hal.h"
#include "util.h"

static int sd_hc;   /* block addressed (SDHC/SDXC) */

static void deselect(void) { sd_cs(0); spi1_xfer(0xFF); }

static uint8_t cmd(uint8_t idx, uint32_t arg) {
    uint8_t crc = idx == 0 ? 0x95 : idx == 8 ? 0x87 : 0x01;
    uint8_t f[6] = {(uint8_t)(0x40 | idx), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
                    (uint8_t)(arg >> 8), (uint8_t)arg, crc};
    spi1_write(f, 6);
    uint8_t r = 0xFF;
    for (int i = 0; i < 10 && (r & 0x80); i++) r = spi1_xfer(0xFF);
    return r;
}

static uint32_t addr(uint32_t lba) { return sd_hc ? lba : lba * 512u; }

static int wait_ready(uint32_t timeout_us) {
    uint32_t t0 = hal_now_us();
    while (spi1_xfer(0xFF) != 0xFF)
        if (hal_now_us() - t0 > timeout_us) return -1;
    return 0;
}

int sd_init(void) {
    spi1_config(SPI1_SD_SLOW);
    sd_cs(0);
    for (int i = 0; i < 10; i++) spi1_xfer(0xFF);       /* >= 74 clocks, CS high */

    uint8_t r = 0;
    for (int i = 0; i < 10 && r != 0x01; i++) { sd_cs(1); r = cmd(0, 0); deselect(); }
    if (r != 0x01) return -1;

    sd_cs(1);
    r = cmd(8, 0x1AA);
    uint8_t r7[4];
    for (int i = 0; i < 4; i++) r7[i] = spi1_xfer(0xFF);
    deselect();
    int v2 = r == 0x01 && r7[2] == 0x01 && r7[3] == 0xAA;

    uint32_t t0 = hal_now_us();
    do {
        sd_cs(1); cmd(55, 0); deselect();
        sd_cs(1); r = cmd(41, v2 ? 0x40000000u : 0); deselect();
        if (hal_now_us() - t0 > 1000000) return -2;
    } while (r != 0x00);

    sd_hc = 0;
    if (v2) {
        sd_cs(1);
        r = cmd(58, 0);
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = spi1_xfer(0xFF);
        deselect();
        if (r != 0) return -3;
        sd_hc = (ocr[0] & 0x40) != 0;
    }
    if (!sd_hc) { sd_cs(1); r = cmd(16, 512); deselect(); if (r) return -4; }

    spi1_config(SPI1_SD_FAST);
    return 0;
}

int sd_read(uint32_t lba, uint8_t *buf) {
    spi1_config(SPI1_SD_FAST);
    sd_cs(1);
    if (wait_ready(500000) || cmd(17, addr(lba)) != 0) { deselect(); return -1; }
    uint32_t t0 = hal_now_us();
    uint8_t tok;
    while ((tok = spi1_xfer(0xFF)) == 0xFF)
        if (hal_now_us() - t0 > 200000) { deselect(); return -2; }
    if (tok != 0xFE) { deselect(); return -3; }
    for (int i = 0; i < 512; i++) buf[i] = spi1_xfer(0xFF);
    spi1_xfer(0xFF); spi1_xfer(0xFF);   /* CRC */
    deselect();
    return 0;
}

int sd_write(uint32_t lba, const uint8_t *buf) {
    spi1_config(SPI1_SD_FAST);
    int r = sd_step_cmd24(lba, buf);
    if (r) return r;
    sd_cs(1);
    r = wait_ready(500000);
    deselect();
    return r;
}

int sd_step_busy(void) {
    sd_cs(1);
    uint8_t b = spi1_xfer(0xFF);
    deselect();
    return b == 0xFF ? 0 : 1;
}

int sd_step_cmd25(uint32_t lba) {
    sd_cs(1);
    uint8_t r = cmd(25, addr(lba));
    deselect();
    return r == 0 ? 0 : -1;
}

static int send_block(uint8_t token, const uint8_t *buf) {
    spi1_xfer(token);
    spi1_write(buf, 512);
    spi1_xfer(0xFF); spi1_xfer(0xFF);   /* CRC (ignored in SPI mode) */
    uint8_t resp = spi1_xfer(0xFF);
    return (resp & 0x1F) == 0x05 ? 0 : -1;
}

int sd_step_block(const uint8_t *buf) {
    sd_cs(1);
    int r = send_block(0xFC, buf);
    deselect();
    return r;
}

int sd_step_stop(void) {
    sd_cs(1);
    spi1_xfer(0xFD);
    spi1_xfer(0xFF);
    deselect();
    return 0;
}

int sd_step_cmd24(uint32_t lba, const uint8_t *buf) {
    sd_cs(1);
    if (cmd(24, addr(lba)) != 0) { deselect(); return -1; }
    spi1_xfer(0xFF);
    int r = send_block(0xFE, buf);
    deselect();
    return r;
}
