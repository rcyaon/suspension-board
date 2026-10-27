/* TDK ICM-42688-P on dedicated SPI2 (mode 0, 12 MHz). Read synchronously once
 * per bridge frame; ODR 2 kHz so each read normally sees a new sample.
 * INT1 is configured as latched data-ready; the latch is read and cleared in
 * the same burst and reported as the "fresh" flag. */
#include "icm42688.h"
#include "hal.h"
#include "util.h"

#define R_DEVICE_CONFIG 0x11
#define R_INT_CONFIG    0x14
#define R_TEMP_DATA1    0x1D
#define R_INT_STATUS    0x2D
#define R_PWR_MGMT0     0x4E
#define R_GYRO_CONFIG0  0x4F
#define R_ACCEL_CONFIG0 0x50
#define R_INT_CONFIG1   0x64
#define R_INT_SOURCE0   0x65
#define R_WHO_AM_I      0x75

static void wr(uint8_t reg, uint8_t v) {
    uint8_t tx[2] = {reg, v}, rx[2];
    imu_cs(1); spi2_xfer_buf(tx, rx, 2); imu_cs(0);
}
static uint8_t rd(uint8_t reg) {
    uint8_t tx[2] = {(uint8_t)(reg | 0x80), 0}, rx[2];
    imu_cs(1); spi2_xfer_buf(tx, rx, 2); imu_cs(0);
    return rx[1];
}

int imu_init(void) {
    wr(R_DEVICE_CONFIG, 0x01);          /* soft reset */
    hal_delay_us(2000);
    if (rd(R_WHO_AM_I) != 0x47) return -1;
    wr(R_INT_CONFIG, 0x07);             /* INT1 latched, push-pull, active high */
    wr(R_INT_CONFIG1, 0x00);            /* required for proper INT pin operation */
    wr(R_INT_SOURCE0, 0x08);            /* UI data ready -> INT1; INT2 unused */
    wr(R_GYRO_CONFIG0, 0x05);           /* +-2000 dps, 2 kHz */
    wr(R_ACCEL_CONFIG0, 0x05);          /* +-16 g, 2 kHz */
    wr(R_PWR_MGMT0, 0x0F);              /* gyro + accel low-noise mode */
    hal_delay_us(1000);
    return 0;
}

void imu_read(imu_sample *s) {
    /* burst 0x1D..0x2D: temp, accel xyz, gyro xyz, (2 skipped), INT_STATUS */
    uint8_t tx[18], rx[18];
    u_memset(tx, 0, sizeof tx);
    tx[0] = R_TEMP_DATA1 | 0x80;
    imu_cs(1); spi2_xfer_buf(tx, rx, sizeof tx); imu_cs(0);
    const uint8_t *d = rx + 1;
    s->temp = (int16_t)((d[0] << 8) | d[1]);
    for (int i = 0; i < 3; i++) {
        s->accel[i] = (int16_t)((d[2 + 2 * i] << 8) | d[3 + 2 * i]);
        s->gyro[i] = (int16_t)((d[8 + 2 * i] << 8) | d[9 + 2 * i]);
    }
    s->fresh = (d[R_INT_STATUS - R_TEMP_DATA1] & 0x08) != 0;
}
