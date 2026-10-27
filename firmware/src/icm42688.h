#ifndef ICM42688_H
#define ICM42688_H
#include <stdint.h>

typedef struct {
    int16_t temp;
    int16_t accel[3];   /* +-16 g, 2048 LSB/g */
    int16_t gyro[3];    /* +-2000 dps, 16.4 LSB/dps */
    int fresh;          /* new sample since last read */
} imu_sample;

int imu_init(void);
void imu_read(imu_sample *s);

#endif
