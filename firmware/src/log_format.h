/* On-card log format, version 1. All integers little-endian.
 *
 *   bytes 0..4095   text header, "key=value" lines, NUL padded
 *   then 512-byte blocks:
 *     [0] type 'D' data | 'E' events | 'T' trailer text
 *     [1] count (records or events; text length/4 for 'T' is unused)
 *     [2..3] reserved
 *     [4..7] block sequence number
 *     [8..]  payload
 *
 * 'D' payload: up to 8 records of 60 bytes (one per ADC frame):
 *     u32 sample      ADC frame counter (gaps = dropped frames)
 *     u32 t_us        DRDY time, 1 MHz from the ADC's own crystal
 *     i24 bridge[8]   raw ADS131M08 codes, CH1..CH8
 *     u16 ads_status
 *     u16 flags       LOGF_*
 *     u16 aux[5]      MCU ADC codes: AUX1..AUX4, battery
 *     i16 accel[3], gyro[3], imu_temp
 * 'E' payload: up to 31 events of 16 bytes: u32 t_us, u32 sample, u16 code, u16 a, u32 b
 */
#ifndef LOG_FORMAT_H
#define LOG_FORMAT_H

#define LOG_HEADER_BYTES 4096
#define LOG_BLOCK 512
#define LOG_BLOCK_HDR 8
#define LOG_REC_BYTES 60
#define LOG_RECS_PER_BLOCK 8
#define LOG_EVT_BYTES 16
#define LOG_EVTS_PER_BLOCK 31

#define LOGF_EXC        (1u << 0)
#define LOGF_SHUNT      (1u << 1)
#define LOGF_PHASE_SHIFT 2           /* 3 bits, PHASE_* */
#define LOGF_CRC_ERR    (1u << 5)
#define LOGF_LATE       (1u << 6)    /* frame read after an SD transaction */
#define LOGF_IMU_STALE  (1u << 7)

enum { PHASE_IDLE, PHASE_SETTLE, PHASE_CAL_START, PHASE_RUN, PHASE_CAL_END };

enum {
    EVT_RUN_START = 1, EVT_PHASE, EVT_SHUNT, EVT_DROP, EVT_OVERRUN, EVT_CRC,
    EVT_CAN_SYNC_TX, EVT_CAN_SYNC_RX, EVT_CAN_CMD_RX, EVT_RUN_STOP, EVT_SD_ERROR
};

#endif
