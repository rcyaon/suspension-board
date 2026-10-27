/* hal.h implemented on a PC with simulated time. SPI transfers advance the
 * clock by their real duration, and the DRDY interrupt "preempts" at byte
 * boundaries, so bus sharing and deadlines behave as on the board. */
#include <stdio.h>
#include <string.h>
#include "../../src/app.h"
#include "../../src/hal.h"
#include "sim.h"

uint64_t sim_t_ns;
int pin_exc, pin_shunt, pin_adc_reset = 1;
uint32_t bus_conflicts;

static int drdy_en, irq_masked, in_isr, irq_pending;
static uint64_t next_drdy_ns;
static int spi1_mode_cur = SPI1_SD_SLOW;
static int adc_sel, sd_sel, imu_sel;

int sim_spi1_mode(void) { return spi1_mode_cur; }
uint64_t sim_drdy_phase_ns(void) { return next_drdy_ns % ads_dev_period_ns(); }

static void service(void) {
    for (;;) {
        if (!ads_dev_converting()) next_drdy_ns = sim_t_ns + ads_dev_period_ns();
        while (ads_dev_converting() && sim_t_ns >= next_drdy_ns) {
            ads_dev_convert(next_drdy_ns);
            next_drdy_ns += ads_dev_period_ns();
            if (drdy_en) irq_pending = 1;
        }
        if (!irq_pending || irq_masked || in_isr) return;
        irq_pending = 0;
        in_isr = 1;
        sim_t_ns += 300;                    /* interrupt entry */
        app_drdy_isr();
        in_isr = 0;
    }
}

void sim_advance_ns(uint64_t ns) { sim_t_ns += ns; service(); }

uint32_t hal_now_us(void) { return (uint32_t)(sim_t_ns / 1000); }
void hal_delay_us(uint32_t us) { sim_advance_ns((uint64_t)us * 1000); }
uint32_t hal_irq_save(void) { uint32_t s = (uint32_t)irq_masked; irq_masked = 1; return s; }
void hal_irq_restore(uint32_t s) { irq_masked = (int)s; if (!s) service(); }
void hal_drdy_irq_enable(int en) { drdy_en = en; irq_pending = 0; }

void spi1_config(int mode) { spi1_mode_cur = mode; }
static uint64_t byte_ns(int gap) {
    double f = spi1_mode_cur == SPI1_ADC ? 12e6 : spi1_mode_cur == SPI1_SD_SLOW ? 375e3 : 24e6;
    return (uint64_t)(8e9 / f) + (gap ? 60 : 0);   /* polled transfers leave a short gap per byte */
}

static uint8_t spi1_byte(uint8_t b, int gap) {
    uint8_t miso = 0xFF;
    if (adc_sel && (sd_sel || sd_dev_driving)) bus_conflicts++;
    if (!sd_sel && sd_dev_driving) sd_dev_driving = 0;   /* a clock with CS high releases DO */
    if (adc_sel) miso &= ads_dev_byte(b);
    if (sd_sel) miso &= sd_dev_byte(b);
    sim_advance_ns(byte_ns(gap));
    return miso;
}
uint8_t spi1_xfer(uint8_t b) { return spi1_byte(b, 1); }
void spi1_write(const uint8_t *buf, uint32_t n) { for (uint32_t i = 0; i < n; i++) spi1_byte(buf[i], 0); }
void spi1_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t n) { for (uint32_t i = 0; i < n; i++) rx[i] = spi1_byte(tx[i], 1); }
void adc_cs(int s) { adc_sel = s; ads_dev_cs(s); sim_t_ns += 50; }
void sd_cs(int s) { sd_sel = s; sd_dev_cs(s); sim_t_ns += 50; }

void spi2_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        rx[i] = imu_sel ? imu_dev_byte(tx[i]) : 0xFF;
        sim_advance_ns(8000 / 12 + 60);
    }
}
void imu_cs(int s) { imu_sel = s; imu_dev_cs(s); sim_t_ns += 50; }

void gpio_adc_reset(int a) { pin_adc_reset = a; ads_dev_reset_pin(a); }
void gpio_exc_en(int on) { pin_exc = on; }
void gpio_shunt(int on) { pin_shunt = on; }
void led_log(int on) { (void)on; }
void led_err(int on) { (void)on; }
int gpio_sd_present(void) { return 1; }
int gpio_vbus(void) { return 0; }

/* MCU ADC: the scan started by aux_trigger() is what the next aux_read() sees */
static uint16_t aux_held[AUX_N];
void aux_trigger(void) {
    double sc[SC_N];
    scenario_at(sim_t_ns * 1e-9 - sim_drive_start_s, sc);
    double v[AUX_N] = {sc[SC_AUX1], 0.0, 0.0, 0.0, 13.2 / 11.0};
    for (int i = 0; i < AUX_N; i++) {
        double c = v[i] / 3.3 * 4095.0 + 0.5;
        aux_held[i] = (uint16_t)(c < 0 ? 0 : c > 4095 ? 4095 : c);
    }
}
void aux_read(uint16_t out[AUX_N]) { memcpy(out, aux_held, sizeof aux_held); }

/* CAN: record what the firmware sends */
uint32_t can_tx_sync, can_tx_start, can_tx_stop, can_tx_status;
int can_init(void) { return 1; }
int can_send(uint16_t id, const uint8_t *d, uint8_t len) {
    (void)d; (void)len;
    if (id == 0x080) can_tx_sync++;
    else if (id == 0x081) can_tx_start++;
    else if (id == 0x082) can_tx_stop++;
    else can_tx_status++;
    return 0;
}

/* UART: output to a log, input from an injected string */
FILE *uart_log;
static char uart_in[256];
static size_t uart_in_h, uart_in_t;
void sim_uart_inject(const char *s) { while (*s) uart_in[uart_in_h++ % sizeof uart_in] = *s++; }
void uart_putc(char c) { if (uart_log && c != '\r') fputc(c, uart_log); }
int uart_getc(void) { return uart_in_t == uart_in_h ? -1 : (uint8_t)uart_in[uart_in_t++ % sizeof uart_in]; }

uint32_t hal_uid(int w) { static const uint32_t id[3] = {0x00350041u, 0x3138510Au, 0x32343337u}; return id[w]; }
