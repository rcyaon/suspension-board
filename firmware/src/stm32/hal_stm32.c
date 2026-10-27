/* hal.h for the STM32F412CEU6 board (pin map: docs/mcu-migration.md).
 * Clocks: HSE 8 MHz -> PLL 96 MHz SYSCLK, 48 MHz USB clock, APB1 48 MHz,
 * APB2 96 MHz. MCO1 outputs HSE/1 on PA8 as the ADS131M08 clock. */
#include "../hal.h"
#include "regs.h"

enum { IN = 0, OUT = 1, AF = 2, AN = 3 };

static void pin(uint32_t port, int n, int mode, int af, int pupd) {
    GPIO_MODER(port) = (GPIO_MODER(port) & ~(3u << (2 * n))) | ((uint32_t)mode << (2 * n));
    GPIO_PUPDR(port) = (GPIO_PUPDR(port) & ~(3u << (2 * n))) | ((uint32_t)pupd << (2 * n));
    GPIO_OSPEEDR(port) |= 3u << (2 * n);
    if (mode == AF) {
        int i = n >> 3, s = (n & 7) * 4;
        GPIO_AFR(port, i) = (GPIO_AFR(port, i) & ~(0xFu << s)) | ((uint32_t)af << s);
    }
}
static void put(uint32_t port, int n, int v) { GPIO_BSRR(port) = v ? (1u << n) : (1u << (n + 16)); }

static void clock_init(void) {
    RCC_APB1ENR |= 1u << 28;                              /* PWR */
    PWR_CR = (PWR_CR & ~(3u << 14)) | (3u << 14);         /* voltage scale 1 */
    RCC_CR |= 1u << 16;                                   /* HSE on */
    for (uint32_t i = 0; !(RCC_CR & (1u << 17)) && i < 1000000; i++) { }
    RCC_PLLCFGR = 8u | (192u << 6) | (0u << 16) | (1u << 22) | (4u << 24) | (2u << 28);
    RCC_CR |= 1u << 24;                                   /* PLL on */
    while (!(RCC_CR & (1u << 25))) { }
    FLASH_ACR = 3u | (1u << 8) | (1u << 9) | (1u << 10);  /* 3 WS, prefetch, caches */
    /* AHB /1, APB1 /2, APB2 /1, MCO1 = HSE /1, SYSCLK = PLL */
    RCC_CFGR = (4u << 10) | (2u << 21) | 2u;
    while (((RCC_CFGR >> 2) & 3u) != 2u) { }
}

static void timer_init(void) {
    RCC_APB1ENR |= 1u << 0;
    TIM_PSC(TIM2) = 96 - 1;                               /* 96 MHz timer clock -> 1 MHz */
    TIM_ARR(TIM2) = 0xFFFFFFFFu;
    TIM_EGR(TIM2) = 1;
    TIM_CR1(TIM2) = 1;
}

uint32_t hal_now_us(void) { return TIM_CNT(TIM2); }
void hal_delay_us(uint32_t us) { uint32_t t0 = TIM_CNT(TIM2); while (TIM_CNT(TIM2) - t0 < us) { } }

uint32_t hal_irq_save(void) {
    uint32_t p;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(p) :: "memory");
    return p;
}
void hal_irq_restore(uint32_t s) { __asm volatile("msr primask, %0" :: "r"(s) : "memory"); }

/* ---- GPIO ---- */
void gpio_adc_reset(int a) { put(GPIOB, 2, !a); }
void gpio_exc_en(int on) { put(GPIOB, 4, on); }
void gpio_shunt(int on) { put(GPIOB, 3, on); }
void led_log(int on) { put(GPIOB, 10, on); }
void led_err(int on) { put(GPIOB, 5, on); }
int gpio_sd_present(void) { return !(GPIO_IDR(GPIOC) & (1u << 13)); }   /* switch to GND when inserted */
int gpio_vbus(void) { return (GPIO_IDR(GPIOA) >> 9) & 1; }
void adc_cs(int s) { put(GPIOA, 15, !s); }
void sd_cs(int s) { put(GPIOA, 10, !s); }
void imu_cs(int s) { put(GPIOB, 12, !s); }

static void gpio_init(void) {
    RCC_AHB1ENR |= (1u << 0) | (1u << 1) | (1u << 2) | (1u << 7) | (1u << 22);  /* GPIOA/B/C/H, DMA2 */
    /* outputs first to safe levels: CS high, excitation/shunt off, ADC in reset */
    put(GPIOA, 10, 1); put(GPIOA, 15, 1); put(GPIOB, 12, 1);
    put(GPIOB, 2, 0); put(GPIOB, 3, 0); put(GPIOB, 4, 0); put(GPIOB, 5, 0); put(GPIOB, 10, 0);
    pin(GPIOA, 10, OUT, 0, 0); pin(GPIOA, 15, OUT, 0, 0);          /* SD_CS, ADC_CS (PA15 leaves JTDI) */
    pin(GPIOB, 2, OUT, 0, 0); pin(GPIOB, 3, OUT, 0, 0);            /* ADC_SYNC_RST, SHUNT_CAL (PB3 leaves JTDO) */
    pin(GPIOB, 4, OUT, 0, 0); pin(GPIOB, 5, OUT, 0, 0);            /* EXC_EN (PB4 leaves NJTRST), LED_ERR */
    pin(GPIOB, 10, OUT, 0, 0); pin(GPIOB, 12, OUT, 0, 0);          /* LED_LOG, IMU_CS */
    pin(GPIOA, 0, AN, 0, 0); pin(GPIOA, 1, AN, 0, 0); pin(GPIOA, 4, AN, 0, 0);
    pin(GPIOB, 0, AN, 0, 0); pin(GPIOB, 1, AN, 0, 0);
    pin(GPIOA, 2, AF, 7, 0); pin(GPIOA, 3, AF, 7, 1);              /* USART2 */
    pin(GPIOA, 5, AF, 5, 0); pin(GPIOA, 6, AF, 5, 0); pin(GPIOA, 7, AF, 5, 0);   /* SPI1 */
    pin(GPIOB, 13, AF, 5, 0); pin(GPIOB, 14, AF, 5, 0); pin(GPIOB, 15, AF, 5, 0); /* SPI2 */
    pin(GPIOB, 8, AF, 8, 1); pin(GPIOB, 9, AF, 8, 0);              /* CAN1 RX/TX */
    pin(GPIOA, 8, AF, 0, 0);                                       /* MCO1 -> ADC CLKIN */
    pin(GPIOA, 9, IN, 0, 0);                                       /* VBUS_SENSE */
    pin(GPIOC, 13, IN, 0, 1);                                      /* SD_DET, pull-up */
    pin(GPIOC, 14, IN, 0, 0); pin(GPIOC, 15, IN, 0, 0);            /* ADC_DRDY, IMU_INT1 */
}

/* ---- SPI ---- */
static int spi1_mode = -1;
void spi1_config(int mode) {
    if (mode == spi1_mode) return;
    while (SPI_SR(SPI1) & SPI_SR_BSY) { }
    SPI_CR1(SPI1) = 0;
    /* ADC: mode 1, 96/8 = 12 MHz. SD: mode 0, 96/256 = 375 kHz init, 96/4 = 24 MHz data */
    uint32_t br = mode == SPI1_ADC ? 2u : mode == SPI1_SD_SLOW ? 7u : 1u;
    uint32_t cpha = mode == SPI1_ADC ? 1u : 0u;
    SPI_CR1(SPI1) = cpha | (1u << 2) | (br << 3) | (1u << 8) | (1u << 9);
    SPI_CR1(SPI1) |= 1u << 6;
    spi1_mode = mode;
}

static uint8_t xfer(uint32_t spi, uint8_t b) {
    while (!(SPI_SR(spi) & SPI_SR_TXE)) { }
    *(volatile uint8_t *)&SPI_DR(spi) = b;
    while (!(SPI_SR(spi) & SPI_SR_RXNE)) { }
    return *(volatile uint8_t *)&SPI_DR(spi);
}
uint8_t spi1_xfer(uint8_t b) { return xfer(SPI1, b); }
void spi1_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t n) { for (uint32_t i = 0; i < n; i++) rx[i] = xfer(SPI1, tx[i]); }
void spi1_write(const uint8_t *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        while (!(SPI_SR(SPI1) & SPI_SR_TXE)) { }
        *(volatile uint8_t *)&SPI_DR(SPI1) = buf[i];
    }
    while (!(SPI_SR(SPI1) & SPI_SR_TXE)) { }
    while (SPI_SR(SPI1) & SPI_SR_BSY) { }
    (void)SPI_DR(SPI1); (void)SPI_SR(SPI1);           /* clear RXNE/OVR */
}
void spi2_xfer_buf(const uint8_t *tx, uint8_t *rx, uint32_t n) { for (uint32_t i = 0; i < n; i++) rx[i] = xfer(SPI2, tx[i]); }

static void spi_init(void) {
    RCC_APB2ENR |= 1u << 12;
    RCC_APB1ENR |= 1u << 14;
    spi1_config(SPI1_SD_SLOW);
    /* SPI2: mode 0, 48/4 = 12 MHz */
    SPI_CR1(SPI2) = (1u << 2) | (1u << 3) | (1u << 8) | (1u << 9);
    SPI_CR1(SPI2) |= 1u << 6;
}

/* ---- DRDY interrupt ---- */
void hal_drdy_irq_enable(int en) {
    if (en) { EXTI_PR = 1u << 14; EXTI_IMR |= 1u << 14; }
    else EXTI_IMR &= ~(1u << 14);
}
void EXTI15_10_IRQHandler(void) {
    if (EXTI_PR & (1u << 14)) { EXTI_PR = 1u << 14; app_drdy_isr(); }
}
static void exti_init(void) {
    RCC_APB2ENR |= 1u << 14;
    SYSCFG_EXTICR(3) = (SYSCFG_EXTICR(3) & ~(0xFu << 8)) | (2u << 8);   /* EXTI14 = PC14 */
    EXTI_FTSR |= 1u << 14;
    NVIC_IPR8(IRQ_EXTI15_10) = 0x00;                                    /* highest */
    NVIC_ISER(IRQ_EXTI15_10 / 32) = 1u << (IRQ_EXTI15_10 % 32);
}

/* ---- MCU ADC: 5-channel scan into a circular DMA buffer ---- */
static volatile uint16_t aux_buf[AUX_N];
static void aux_init(void) {
    RCC_APB2ENR |= 1u << 8;
    ADC_CCR = (ADC_CCR & ~(3u << 16)) | (1u << 16);                     /* 96/4 = 24 MHz */
    ADC_CR1(ADC1) = 1u << 8;                                            /* scan */
    ADC_SMPR2(ADC1) = (6u << 0) | (6u << 3) | (6u << 12) | (6u << 24) | (6u << 27);   /* 144 cycles */
    ADC_SQR1(ADC1) = (AUX_N - 1u) << 20;
    ADC_SQR3(ADC1) = 0u | (1u << 5) | (4u << 10) | (8u << 15) | (9u << 20);
    DMA_S_CR(DMA2, 4) = 0;
    DMA_S_PAR(DMA2, 4) = ADC_DR_ADDR(ADC1);
    DMA_S_M0AR(DMA2, 4) = (uint32_t)aux_buf;
    DMA_S_NDTR(DMA2, 4) = AUX_N;
    DMA_S_CR(DMA2, 4) = (0u << 25) | (1u << 13) | (1u << 11) | (1u << 10) | (1u << 8) | 1u;
    ADC_CR2(ADC1) = (1u << 8) | (1u << 9) | 1u;                         /* DMA, DDS, ADON */
}
void aux_trigger(void) { ADC_CR2(ADC1) |= 1u << 30; }
void aux_read(uint16_t out[AUX_N]) { for (int i = 0; i < AUX_N; i++) out[i] = aux_buf[i]; }

/* ---- CAN: 500 kbit/s from 48 MHz APB1: BRP 6, 1+13+2 tq, 87.5 % ---- */
int can_init(void) {
    RCC_APB1ENR |= 1u << 25;
    CAN_MCR = 1u;                                          /* INRQ, leave sleep */
    for (uint32_t t0 = hal_now_us(); !(CAN_MSR & 1u); ) if (hal_now_us() - t0 > 10000) return 0;
    CAN_BTR = (0u << 24) | (1u << 20) | (12u << 16) | 5u;
    CAN_FMR |= 1u;
    CAN_FM1R = 0; CAN_FS1R = 1; CAN_FFA1R = 0; CAN_F0R1 = 0; CAN_F0R2 = 0; CAN_FA1R = 1;   /* accept all */
    CAN_FMR &= ~1u;
    CAN_MCR = (1u << 6) | (1u << 2);                       /* auto bus-off recovery, TX FIFO order */
    /* leaving init needs 11 recessive bits; fails if the transceiver is unpowered (USB only) */
    for (uint32_t t0 = hal_now_us(); CAN_MSR & 1u; ) if (hal_now_us() - t0 > 10000) return 0;
    CAN_IER = 1u << 1;
    NVIC_IPR8(IRQ_CAN1_RX0) = 0x40;
    NVIC_ISER(IRQ_CAN1_RX0 / 32) = 1u << (IRQ_CAN1_RX0 % 32);
    return 1;
}

int can_send(uint16_t id, const uint8_t *d, uint8_t len) {
    uint32_t tsr = CAN_TSR;
    int m = (tsr & (1u << 26)) ? 0 : (tsr & (1u << 27)) ? 1 : (tsr & (1u << 28)) ? 2 : -1;
    if (m < 0) return -1;
    CAN_TDTR(m) = len;
    CAN_TDLR(m) = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    CAN_TDHR(m) = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
    CAN_TIR(m) = ((uint32_t)id << 21) | 1u;
    return 0;
}

void CAN1_RX0_IRQHandler(void) {
    while (CAN_RF0R & 3u) {
        uint32_t t = hal_now_us();
        uint8_t d[8];
        uint32_t lo = CAN_RDL0R, hi = CAN_RDH0R;
        for (int i = 0; i < 4; i++) { d[i] = (uint8_t)(lo >> (8 * i)); d[4 + i] = (uint8_t)(hi >> (8 * i)); }
        uint32_t rir = CAN_RI0R;
        uint8_t len = (uint8_t)(CAN_RDT0R & 0xF);
        CAN_RF0R = 1u << 5;
        if (!(rir & 4u)) app_can_rx((uint16_t)(rir >> 21), d, len, t);
    }
}

/* ---- UART2 console, 115200 8N1, interrupt driven ---- */
static volatile uint8_t txq[256], rxq[64];
static volatile uint32_t tx_h, tx_t, rx_h, rx_t;
static void uart_init(void) {
    RCC_APB1ENR |= 1u << 17;
    USART_BRR(USART2) = 417;                                /* 48 MHz / 115200 */
    USART_CR1(USART2) = (1u << 13) | (1u << 3) | (1u << 2) | (1u << 5);
    NVIC_IPR8(IRQ_USART2) = 0x80;
    NVIC_ISER(IRQ_USART2 / 32) = 1u << (IRQ_USART2 % 32);
}
void uart_putc(char c) {
    if (tx_h - tx_t >= sizeof txq) return;                  /* drop rather than block */
    txq[tx_h++ % sizeof txq] = (uint8_t)c;
    USART_CR1(USART2) |= 1u << 7;
}
int uart_getc(void) { return rx_h == rx_t ? -1 : rxq[rx_t++ % sizeof rxq]; }
void USART2_IRQHandler(void) {
    uint32_t sr = USART_SR(USART2);
    if (sr & (1u << 5)) { uint8_t c = (uint8_t)USART_DR(USART2); if (rx_h - rx_t < sizeof rxq) rxq[rx_h++ % sizeof rxq] = c; }
    if (sr & (1u << 3)) (void)USART_DR(USART2);             /* overrun */
    if ((sr & (1u << 7)) && (USART_CR1(USART2) & (1u << 7))) {
        if (tx_h != tx_t) USART_DR(USART2) = txq[tx_t++ % sizeof txq];
        else USART_CR1(USART2) &= ~(1u << 7);
    }
}

uint32_t hal_uid(int w) { return R32(UID_BASE + 4u * (uint32_t)w); }

void hal_init(void) {
    clock_init();
    timer_init();
    gpio_init();       /* MCO1 starts here, with the ADC still held in reset */
    spi_init();
    exti_init();
    aux_init();
    uart_init();
}
