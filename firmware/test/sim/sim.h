/* Simulator internals shared by the HAL and the device models. */
#ifndef SIM_H
#define SIM_H
#include <stdint.h>

extern uint64_t sim_t_ns;
void sim_advance_ns(uint64_t ns);
int sim_spi1_mode(void);

/* scenario: truth signals at scenario time (s), clamped to the file range */
enum { SC_CH1, SC_CH8 = SC_CH1 + 7, SC_AUX1, SC_AX, SC_AY, SC_AZ, SC_GX, SC_GY, SC_GZ, SC_N };
int scenario_load(const char *path);
void scenario_at(double t_s, double out[SC_N]);
extern double sim_drive_start_s;       /* sim time at which scenario time 0 plays */

/* board state seen by devices */
extern int pin_exc, pin_shunt, pin_adc_reset;
extern double sim_exc_volts;           /* this simulated board's actual excitation */

/* ADS131M08 model */
void ads_dev_cs(int selected);
uint8_t ads_dev_byte(uint8_t mosi);
void ads_dev_reset_pin(int low);
int ads_dev_converting(void);
uint64_t ads_dev_period_ns(void);
void ads_dev_convert(uint64_t t_ns);
extern uint32_t ads_err_mode, ads_err_frame, ads_missed;
int sim_logging(void);             /* firmware is in a logging state */

/* microSD model */
int sd_dev_open(const char *img, uint64_t bytes);
int sd_dev_save(const char *img);
void sd_dev_cs(int selected);
uint8_t sd_dev_byte(uint8_t mosi);
extern int sd_dev_driving;
extern uint32_t sd_err_proto, sd_err_mode, sd_blocks_written, sd_long_stalls;
extern uint32_t sd_stall_every_blocks, sd_stall_us, sd_big_stall_at_block, sd_big_stall_us;

/* ICM-42688 model */
void imu_dev_cs(int selected);
uint8_t imu_dev_byte(uint8_t mosi);
extern uint32_t imu_err;

#endif
