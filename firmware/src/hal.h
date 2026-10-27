/* Hardware abstraction. Everything above this line is plain C that also runs
 * in the PC simulator (test/sim). hal_stm32.c implements it for the board. */
#ifndef HAL_H
#define HAL_H
#include <stdint.h>

/* time: free-running 1 MHz counter from the same 8 MHz crystal as the ADC */
uint32_t hal_now_us(void);
void hal_delay_us(uint32_t us);

/* interrupts */
uint32_t hal_irq_save(void);
void hal_irq_restore(uint32_t s);
void hal_drdy_irq_enable(int en);   /* ADS131M08 DRDY falling edge on PC14 */

/* SPI1: shared by ADC (mode 1) and microSD (mode 0) */
enum { SPI1_ADC, SPI1_SD_SLOW, SPI1_SD_FAST };
void spi1_config(int mode);
uint8_t spi1_xfer(uint8_t b);
void spi1_write(const uint8_t *buf, uint32_t n);   /* transmit only */
void spi1_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t n);
void adc_cs(int selected);
void sd_cs(int selected);

/* SPI2: IMU only */
void spi2_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t n);
void imu_cs(int selected);

/* GPIO */
void gpio_adc_reset(int asserted);  /* PB2, active low on the pin */
void gpio_exc_en(int on);           /* PB4 */
void gpio_shunt(int on);            /* PB3, all 8 shunt FETs */
void led_log(int on);               /* PB10 */
void led_err(int on);               /* PB5 */
int gpio_sd_present(void);          /* PC13 */
int gpio_vbus(void);                /* PA9 */

/* MCU ADC1: AUX1-4 + battery, one scan per bridge frame */
#define AUX_N 5
void aux_trigger(void);
void aux_read(uint16_t out[AUX_N]);

/* CAN (bxCAN1, 500 kbit/s classic). can_init returns 0 if the bus/transceiver
 * is not available (USB-only power). Received frames arrive via app_can_rx. */
int can_init(void);
int can_send(uint16_t id, const uint8_t *data, uint8_t len);

/* UART2 console */
void uart_putc(char c);
int uart_getc(void);                /* -1 when empty */

uint32_t hal_uid(int word);         /* 96-bit unique ID, word 0..2 */

/* callbacks into the application (called from interrupt context) */
void app_drdy_isr(void);
void app_can_rx(uint16_t id, const uint8_t *data, uint8_t len, uint32_t t_us);

#endif
