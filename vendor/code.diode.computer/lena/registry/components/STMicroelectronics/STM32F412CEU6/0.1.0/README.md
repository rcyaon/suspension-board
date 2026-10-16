# STM32F412CEU6

STMicroelectronics STM32F412CE Arm Cortex-M4F microcontroller: 100 MHz,
512 KB flash, 256 KB SRAM, UFQFPN48 7 x 7 mm, 0.5 mm pitch, 5.6 x 5.6 mm
exposed pad (ST package A0B9), -40 to +85 °C.

Primitive package: the component only. Decoupling, VCAP capacitor, crystals,
reset, BOOT0 strap, and debug header belong to the integrating design.

Datasheet: [DS11139 Rev 9](https://www.st.com/resource/en/datasheet/stm32f412ce.pdf).

## Usage

```zen
Mcu = Module("@registry/components/STMicroelectronics/STM32F412CEU6/STM32F412CEU6.zen")

Mcu(
    name = "MCU",
    VDD = VDD_3V3,
    VDDA = VDDA_3V3,  # same potential as VDD (e.g. via ferrite)
    VBAT = VDD_3V3,   # or a backup cell, 1.65-3.6 V
    VSS = GND,
    VSSA = GND,
    EP = GND,
    VCAP = VCAP_NODE, # 4.7 uF, ESR < 1 ohm, to GND only
    BOOT0 = BOOT0_NET,
    PA13 = SWDIO,
    PA14 = SWCLK,
)
```

## Integration notes (DS11139)

| Domain | Pins | Requirement |
| --- | --- | --- |
| VDD / VSS | 24/23, 36/35, 48/47 | 100 nF per pair + one 4.7 µF ceramic on one VDD pin (Fig. 21). 1.8–3.6 V: no PDR_ON pin on this package, supervisor always on (§3.18.1). |
| VDDA/VREF+ / VSSA/VREF- | 9 / 8 | 100 nF + 1 µF. VDDA = VDD potential (≤ 300 mV during ramps), VSSA = VSS (Table 16, Fig. 21). ADC ≥ 2.4 V for 2.4 Msps. |
| VBAT | 1 | 1.65–3.6 V backup domain; tie to VDD if unused. |
| VCAP_1 | 22 | Core regulator (V12) output. Single VCAP package: one 4.7 µF, ESR < 1 Ω to VSS (p84 Table 18). The 2 × 2.2 µF (ESR < 2 Ω) option is only for 100/144-pin packages with VCAP_2. Do not load. |
| EP | 49 | Exposed die pad — solder to PCB ground (Fig. 63 note 3). No thermal vias in the footprint; ST does not require them. |

- USB FS (PA11/PA12): VDDUSB is not bonded out on this package; USB requires
  VDD ≥ 3.0 V (Table 16 note 5 / VDDUSB row).
- PC13–PC15 sit on the backup power switch: ≤ 2 MHz, ≤ 30 pF, never source
  current (Table 9 note 2).
- BOOT0 is a dedicated input (0–9 V); PB2 doubles as BOOT1.
- FSMC/SDIO AFs are listed as in Table 11; on a 48-pin device only the bonded
  subset is usable, so check CubeMX/RM0402 before relying on FSMC.
- The FT I/Os are 5 V tolerant except in analog or oscillator mode.

- DS11139 Fig. 12 (p43) labels pin 6 "PH0-OSC_OUT"; this is a typo — Table 9
  gives PH1-OSC_OUT, which the symbol uses.

## Footprint

`ST_UFQFPN-48-1EP_7x7mm_P0.5mm_EP5.6x5.6mm` is generated from ST's land
pattern (Fig. 64): 48 lands 0.30 × 0.55 mm at 0.5 mm pitch, 6.75 mm
center-to-center rows, 5.6 × 5.6 mm EP land (0.30 mm EP-to-land gap), EP paste
split into 4 × 4 windows (~65 % coverage). Body and EP limits from Table 101
(A 0.55 nom / 0.60 max). The STEP model is generated from Table 101 nominal
dimensions and embedded in the footprint (bounding box 7.0 × 7.0 × 0.60 mm).

## Pin map (UFQFPN48, Table 9 + Table 11)

The symbol carries each AF/additional function as a KiCad pin alternate.

| Pin | Name | Type | I/O | Alternate functions (AF number) | Additional functions |
| --- | --- | --- | --- | --- | --- |
| 1 | VBAT | S | - | - | VBAT |
| 2 | PC13 | I/O | FT | AF15: EVENTOUT | TAMP_1 |
| 3 | PC14-OSC32_IN | I/O | FT | AF15: EVENTOUT | OSC32_IN |
| 4 | PC15-OSC32_OUT | I/O | FT | AF15: EVENTOUT | OSC32_OUT |
| 5 | PH0-OSC_IN | I/O | FT | AF15: EVENTOUT | OSC_IN |
| 6 | PH1-OSC_OUT | I/O | FT | AF15: EVENTOUT | OSC_OUT |
| 7 | NRST | I/O | RST | - | NRST |
| 8 | VSSA/VREF- | S | - | - | - |
| 9 | VDDA/VREF+ | S | - | - | - |
| 10 | PA0 | I/O | FT | AF1: TIM2_CH1/TIM2_ETR; AF2: TIM5_CH1; AF3: TIM8_ETR; AF7: USART2_CTS; AF15: EVENTOUT | ADC1_0, WKUP1 |
| 11 | PA1 | I/O | FT | AF1: TIM2_CH2; AF2: TIM5_CH2; AF5: SPI4_MOSI/I2S4_SD; AF7: USART2_RTS; AF9: QUADSPI_BK1_IO3; AF15: EVENTOUT | ADC1_1 |
| 12 | PA2 | I/O | FT | AF1: TIM2_CH3; AF2: TIM5_CH3; AF3: TIM9_CH1; AF5: I2S2_CKIN; AF7: USART2_TX; AF12: FSMC_D4/FSMC_DA4; AF15: EVENTOUT | ADC1_2 |
| 13 | PA3 | I/O | FT | AF1: TIM2_CH4; AF2: TIM5_CH4; AF3: TIM9_CH2; AF5: I2S2_MCK; AF7: USART2_RX; AF12: FSMC_D5/FSMC_DA5; AF15: EVENTOUT | ADC1_3 |
| 14 | PA4 | I/O | FT | AF5: SPI1_NSS/I2S1_WS; AF6: SPI3_NSS/I2S3_WS; AF7: USART2_CK; AF8: DFSDM1_DATIN1; AF12: FSMC_D6/FSMC_DA6; AF15: EVENTOUT | ADC1_4 |
| 15 | PA5 | I/O | FT | AF1: TIM2_CH1/TIM2_ETR; AF3: TIM8_CH1N; AF5: SPI1_SCK/I2S1_CK; AF8: DFSDM1_CKIN1; AF12: FSMC_D7/FSMC_DA7; AF15: EVENTOUT | ADC1_5 |
| 16 | PA6 | I/O | FT | AF1: TIM1_BKIN; AF2: TIM3_CH1; AF3: TIM8_BKIN; AF5: SPI1_MISO; AF6: I2S2_MCK; AF9: TIM13_CH1; AF10: QUADSPI_BK2_IO0; AF12: SDIO_CMD; AF15: EVENTOUT | ADC1_6 |
| 17 | PA7 | I/O | FT | AF1: TIM1_CH1N; AF2: TIM3_CH2; AF3: TIM8_CH1N; AF5: SPI1_MOSI/I2S1_SD; AF9: TIM14_CH1; AF10: QUADSPI_BK2_IO1; AF15: EVENTOUT | ADC1_7 |
| 18 | PB0 | I/O | FT | AF1: TIM1_CH2N; AF2: TIM3_CH3; AF3: TIM8_CH2N; AF6: SPI5_SCK/I2S5_CK; AF15: EVENTOUT | ADC1_8 |
| 19 | PB1 | I/O | FT | AF1: TIM1_CH3N; AF2: TIM3_CH4; AF3: TIM8_CH3N; AF6: SPI5_NSS/I2S5_WS; AF8: DFSDM1_DATIN0; AF9: QUADSPI_CLK; AF15: EVENTOUT | ADC1_9 |
| 20 | PB2 | I/O | FT | AF6: DFSDM1_CKIN0; AF9: QUADSPI_CLK; AF15: EVENTOUT | BOOT1 |
| 21 | PB10 | I/O | FT | AF1: TIM2_CH3; AF4: I2C2_SCL; AF5: SPI2_SCK/I2S2_CK; AF6: I2S3_MCK; AF7: USART3_TX; AF9: I2CFMP1_SCL; AF12: SDIO_D7; AF15: EVENTOUT | - |
| 22 | VCAP_1 | S | - | - | - |
| 23 | VSS | S | - | - | - |
| 24 | VDD | S | - | - | - |
| 25 | PB12 | I/O | FT | AF1: TIM1_BKIN; AF4: I2C2_SMBA; AF5: SPI2_NSS/I2S2_WS; AF6: SPI4_NSS/I2S4_WS; AF7: SPI3_SCK/I2S3_CK; AF8: USART3_CK; AF9: CAN2_RX; AF10: DFSDM1_DATIN1; AF12: FSMC_D13/FSMC_DA13; AF15: EVENTOUT | - |
| 26 | PB13 | I/O | FT | AF1: TIM1_CH1N; AF4: I2CFMP1_SMBA; AF5: SPI2_SCK/I2S2_CK; AF6: SPI4_SCK/I2S4_CK; AF8: USART3_CTS; AF9: CAN2_TX; AF10: DFSDM1_CKIN1; AF15: EVENTOUT | - |
| 27 | PB14 | I/O | FT | AF1: TIM1_CH2N; AF3: TIM8_CH2N; AF4: I2CFMP1_SDA; AF5: SPI2_MISO; AF6: I2S2ext_SD; AF7: USART3_RTS; AF8: DFSDM1_DATIN2; AF9: TIM12_CH1; AF10: FSMC_D0; AF12: SDIO_D6; AF15: EVENTOUT | - |
| 28 | PB15 | I/O | FT | AF0: RTC_50Hz; AF1: TIM1_CH3N; AF3: TIM8_CH3N; AF4: I2CFMP1_SCL; AF5: SPI2_MOSI/I2S2_SD; AF8: DFSDM1_CKIN2; AF9: TIM12_CH2; AF12: SDIO_CK; AF15: EVENTOUT | - |
| 29 | PA8 | I/O | FT | AF0: MCO_1; AF1: TIM1_CH1; AF4: I2C3_SCL; AF7: USART1_CK; AF10: USB_FS_SOF; AF12: SDIO_D1; AF15: EVENTOUT | - |
| 30 | PA9 | I/O | FT | AF1: TIM1_CH2; AF4: I2C3_SMBA; AF7: USART1_TX; AF10: USB_FS_VBUS; AF12: SDIO_D2; AF15: EVENTOUT | - |
| 31 | PA10 | I/O | FT | AF1: TIM1_CH3; AF6: SPI5_MOSI/I2S5_SD; AF7: USART1_RX; AF10: USB_FS_ID; AF15: EVENTOUT | - |
| 32 | PA11 | I/O | FT | AF1: TIM1_CH4; AF6: SPI4_MISO; AF7: USART1_CTS; AF8: USART6_TX; AF9: CAN1_RX; AF10: USB_FS_DM; AF15: EVENTOUT | - |
| 33 | PA12 | I/O | FT | AF1: TIM1_ETR; AF6: SPI5_MISO; AF7: USART1_RTS; AF8: USART6_RX; AF9: CAN1_TX; AF10: USB_FS_DP; AF15: EVENTOUT | - |
| 34 | PA13 | I/O | FT | AF0: JTMS-SWDIO; AF15: EVENTOUT | - |
| 35 | VSS | S | - | - | - |
| 36 | VDD | S | - | - | - |
| 37 | PA14 | I/O | FT | AF0: JTCK-SWCLK; AF15: EVENTOUT | - |
| 38 | PA15 | I/O | FT | AF0: JTDI; AF1: TIM2_CH1/TIM2_ETR; AF5: SPI1_NSS/I2S1_WS; AF6: SPI3_NSS/I2S3_WS; AF7: USART1_TX; AF15: EVENTOUT | - |
| 39 | PB3 | I/O | FT | AF0: JTDO-SWO; AF1: TIM2_CH2; AF4: I2CFMP1_SDA; AF5: SPI1_SCK/I2S1_CK; AF6: SPI3_SCK/I2S3_CK; AF7: USART1_RX; AF9: I2C2_SDA; AF15: EVENTOUT | - |
| 40 | PB4 | I/O | FT | AF0: JTRST; AF2: TIM3_CH1; AF5: SPI1_MISO; AF6: SPI3_MISO; AF7: I2S3ext_SD; AF9: I2C3_SDA; AF12: SDIO_D0; AF15: EVENTOUT | - |
| 41 | PB5 | I/O | FT | AF2: TIM3_CH2; AF4: I2C1_SMBA; AF5: SPI1_MOSI/I2S1_SD; AF6: SPI3_MOSI/I2S3_SD; AF9: CAN2_RX; AF12: SDIO_D3; AF15: EVENTOUT | - |
| 42 | PB6 | I/O | FT | AF2: TIM4_CH1; AF4: I2C1_SCL; AF7: USART1_TX; AF9: CAN2_TX; AF10: QUADSPI_BK1_NCS; AF12: SDIO_D0; AF15: EVENTOUT | - |
| 43 | PB7 | I/O | FT | AF2: TIM4_CH2; AF4: I2C1_SDA; AF7: USART1_RX; AF12: FSMC_NL; AF15: EVENTOUT | - |
| 44 | BOOT0 | I | B | - | VPP |
| 45 | PB8 | I/O | FT | AF2: TIM4_CH3; AF3: TIM10_CH1; AF4: I2C1_SCL; AF6: SPI5_MOSI/I2S5_SD; AF8: CAN1_RX; AF9: I2C3_SDA; AF12: SDIO_D4; AF15: EVENTOUT | - |
| 46 | PB9 | I/O | FT | AF2: TIM4_CH4; AF3: TIM11_CH1; AF4: I2C1_SDA; AF5: SPI2_NSS/I2S2_WS; AF8: CAN1_TX; AF9: I2C2_SDA; AF12: SDIO_D5; AF15: EVENTOUT | - |
| 47 | VSS | S | - | - | - |
| 48 | VDD | S | - | - | - |
| 49 | EP (exposed pad) | - | - | - | connect to PCB ground |

## Sourcing

- Primary: `STM32F412CEU6` (tray).
- Alternative: `STM32F412CEU6TR` (tape and reel, 2500 MOQ) — ordering-only
  packing variant of the same part.
