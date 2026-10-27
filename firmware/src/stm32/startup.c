/* Reset, vector table and main() for the STM32F412CEU6. */
#include <stddef.h>
#include <stdint.h>
#include "../app.h"
#include "regs.h"

void hal_init(void);
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void) {
    hal_init();
    app_init();
    for (;;) app_poll();
}

void Reset_Handler(void) {
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss;) *d++ = 0;
    SCB_CPACR |= 0xFu << 20;                     /* FPU on */
    main();
    for (;;) { }
}

void Default_Handler(void) {
    /* LED_ERR on, then wait for the watchdog/reset */
    R32(GPIOB + 0x18) = 1u << 5;
    for (;;) { }
}

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI15_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CAN1_RX0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

#define NVEC (16 + 97)
__attribute__((section(".isr_vector"), used))
void (*const vectors[NVEC])(void) = {
    [0] = (void (*)(void))&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4 ... 15] = Default_Handler,
    [16 ... 16 + IRQ_CAN1_RX0 - 1] = Default_Handler,
    [16 + IRQ_CAN1_RX0] = CAN1_RX0_IRQHandler,
    [16 + IRQ_CAN1_RX0 + 1 ... 16 + IRQ_USART2 - 1] = Default_Handler,
    [16 + IRQ_USART2] = USART2_IRQHandler,
    [16 + IRQ_USART2 + 1 ... 16 + IRQ_EXTI15_10 - 1] = Default_Handler,
    [16 + IRQ_EXTI15_10] = EXTI15_10_IRQHandler,
    [16 + IRQ_EXTI15_10 + 1 ... NVEC - 1] = Default_Handler,
};

/* the compiler may emit calls to these */
void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int v, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)v; return d; }
