/* Minimal STM32F412 register map (RM0402), only what this firmware uses. */
#ifndef REGS_H
#define REGS_H
#include <stdint.h>

#define R32(a) (*(volatile uint32_t *)(a))

/* RCC */
#define RCC        0x40023800u
#define RCC_CR       R32(RCC + 0x00)
#define RCC_PLLCFGR  R32(RCC + 0x04)
#define RCC_CFGR     R32(RCC + 0x08)
#define RCC_AHB1ENR  R32(RCC + 0x30)
#define RCC_APB1ENR  R32(RCC + 0x40)
#define RCC_APB2ENR  R32(RCC + 0x44)

#define FLASH_ACR    R32(0x40023C00u)
#define PWR_CR       R32(0x40007000u)

/* GPIO */
#define GPIOA 0x40020000u
#define GPIOB 0x40020400u
#define GPIOC 0x40020800u
#define GPIOH 0x40021C00u
#define GPIO_MODER(p)   R32((p) + 0x00)
#define GPIO_OTYPER(p)  R32((p) + 0x04)
#define GPIO_OSPEEDR(p) R32((p) + 0x08)
#define GPIO_PUPDR(p)   R32((p) + 0x0C)
#define GPIO_IDR(p)     R32((p) + 0x10)
#define GPIO_BSRR(p)    R32((p) + 0x18)
#define GPIO_AFR(p, i)  R32((p) + 0x20 + 4 * (i))

/* SPI */
#define SPI1 0x40013000u
#define SPI2 0x40003800u
#define SPI_CR1(s) R32((s) + 0x00)
#define SPI_SR(s)  R32((s) + 0x08)
#define SPI_DR(s)  R32((s) + 0x0C)
#define SPI_SR_RXNE (1u << 0)
#define SPI_SR_TXE  (1u << 1)
#define SPI_SR_BSY  (1u << 7)

/* USART2 */
#define USART2 0x40004400u
#define USART_SR(u)  R32((u) + 0x00)
#define USART_DR(u)  R32((u) + 0x04)
#define USART_BRR(u) R32((u) + 0x08)
#define USART_CR1(u) R32((u) + 0x0C)

/* TIM2 */
#define TIM2 0x40000000u
#define TIM_CR1(t) R32((t) + 0x00)
#define TIM_EGR(t) R32((t) + 0x14)
#define TIM_CNT(t) R32((t) + 0x24)
#define TIM_PSC(t) R32((t) + 0x28)
#define TIM_ARR(t) R32((t) + 0x2C)

/* EXTI / SYSCFG */
#define EXTI_IMR  R32(0x40013C00u)
#define EXTI_FTSR R32(0x40013C0Cu)
#define EXTI_PR   R32(0x40013C14u)
#define SYSCFG_EXTICR(i) R32(0x40013808u + 4 * (i))

/* ADC1 */
#define ADC1 0x40012000u
#define ADC_CR1(a)   R32((a) + 0x04)
#define ADC_CR2(a)   R32((a) + 0x08)
#define ADC_SMPR1(a) R32((a) + 0x0C)
#define ADC_SMPR2(a) R32((a) + 0x10)
#define ADC_SQR1(a)  R32((a) + 0x2C)
#define ADC_SQR3(a)  R32((a) + 0x34)
#define ADC_DR_ADDR(a) ((a) + 0x4C)
#define ADC_CCR      R32(0x40012304u)

/* DMA2 */
#define DMA2 0x40026400u
#define DMA_S_CR(d, s)   R32((d) + 0x10 + 0x18 * (s))
#define DMA_S_NDTR(d, s) R32((d) + 0x14 + 0x18 * (s))
#define DMA_S_PAR(d, s)  R32((d) + 0x18 + 0x18 * (s))
#define DMA_S_M0AR(d, s) R32((d) + 0x1C + 0x18 * (s))

/* bxCAN1 */
#define CAN1 0x40006400u
#define CAN_MCR   R32(CAN1 + 0x000)
#define CAN_MSR   R32(CAN1 + 0x004)
#define CAN_TSR   R32(CAN1 + 0x008)
#define CAN_RF0R  R32(CAN1 + 0x00C)
#define CAN_IER   R32(CAN1 + 0x014)
#define CAN_BTR   R32(CAN1 + 0x01C)
#define CAN_TIR(m)  R32(CAN1 + 0x180 + 0x10 * (m))
#define CAN_TDTR(m) R32(CAN1 + 0x184 + 0x10 * (m))
#define CAN_TDLR(m) R32(CAN1 + 0x188 + 0x10 * (m))
#define CAN_TDHR(m) R32(CAN1 + 0x18C + 0x10 * (m))
#define CAN_RI0R  R32(CAN1 + 0x1B0)
#define CAN_RDT0R R32(CAN1 + 0x1B4)
#define CAN_RDL0R R32(CAN1 + 0x1B8)
#define CAN_RDH0R R32(CAN1 + 0x1BC)
#define CAN_FMR   R32(CAN1 + 0x200)
#define CAN_FM1R  R32(CAN1 + 0x204)
#define CAN_FS1R  R32(CAN1 + 0x20C)
#define CAN_FFA1R R32(CAN1 + 0x214)
#define CAN_FA1R  R32(CAN1 + 0x21C)
#define CAN_F0R1  R32(CAN1 + 0x240)
#define CAN_F0R2  R32(CAN1 + 0x244)

/* core */
#define NVIC_ISER(i) R32(0xE000E100u + 4 * (i))
#define NVIC_IPR8(n) (*(volatile uint8_t *)(0xE000E400u + (n)))
#define SCB_CPACR    R32(0xE000ED88u)
#define UID_BASE     0x1FFF7A10u

#define IRQ_CAN1_RX0  20
#define IRQ_USART2    38
#define IRQ_EXTI15_10 40

#endif
