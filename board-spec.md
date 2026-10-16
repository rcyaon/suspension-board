# suspension board: what it has to do

requirements for the strain-gauge logger on one corner of a baja double wishbone. written as input to diode. the measurement side follows `docs/test-plan.md`. the circuit choices carried over from rev A follow `suspension-load-daq/docs/design-notes.md`, with the mcu changed to the STM32F412CE.

## job

bond strain gauges to the wishbone arms of one corner. log the arm forces, shock travel and chassis motion to a microSD card while the car drives. the logs get calibrated into forces in a fixture and fed into ansys for stress and fatigue.

one board per instrumented corner. the rear corner comes first.

## inputs

### 8 bridge channels

all eight are identical and sampled at the same instant. calibration mixes channels (cross-talk between force directions), so per-channel multiplexing is not acceptable.

| ch | sensor | signal at full scale | gain |
|---|---|---|---|
| 1, 2 | LCA legs, axial full bridge (Poisson) | ±0.08 mV/V (±60 µε) | 128 |
| 3, 4 | LCA legs, bending full bridge | ±1.4 mV/V (±1100 µε), must stay on scale to yield (≈2.7 mV/V) | 32 |
| 5, 6 | UCA legs, axial full bridge | ±0.04 mV/V (±30 µε) | 128 |
| 7 | toe link, axial bridge or instrumented rod end | ±0.5 kN | 32 to 128 |
| 8 | shock inline load cell (0 to 8 kN) | per load cell datasheet, typ. 2 mV/V | 32 |

requirements per channel:

- **bridge type:** full bridge as built. jumper/resistor options for half and quarter bridge (3-wire), with completion resistors equal to the gauge resistance.
- **gauges:** 350 Ω nominal. 120 Ω must still work, at higher excitation current.
- **noise:** ≤ 0.5 µV/V rms at 2 ksps, gain 128, about 0.3 µε on an axial bridge. the axial channels are the hard requirement.
- **anti-alias:** differential RC ahead of the ADC (1 kΩ + 1 kΩ, 10 nF C0G), 1 nF C0G to ground on each input, 0.1 % resistors.
- **flatness:** gain within 0.5 % flat to 500 Hz.
- **shunt calibration:** 100 kΩ switched across one bridge arm by a FET, all channels from one MCU pin. that gives −0.874 mV/V on 350 Ω.
- **connector:** 4 wires (EXC+, EXC−, SIG+, SIG−) plus a QTR lead and shield, on one locking connector per channel.

### 4 aux analog inputs

- **sensors:** any 3-wire ratiometric position sensor, read by the MCU's 12-bit ADC: a linear pot on the shock, a Hall-effect arm-angle sensor (e.g. TI DRV5055), a rotary ride-height sensor or a string pot. linear pots are not rated for mud and dust, so prefer sealed or contactless sensors for field runs (see `docs/baja-discord-notes.md`).
- **5 V option:** a jumper selects 5 V supply for active sensors (brake pressure), with a divider position to bring 0–5 V down to ≤ 3.2 V.
- **protection:** 10 kΩ series, and survives a 14.4 V short on the signal wire with ≤ 5 mA into the pin.
- **bandwidth:** ≥ 200 Hz.

### battery voltage

- divided down to one MCU ADC pin.

## signal chain

| block | part | requirement |
|---|---|---|
| bridge ADC | TI ADS131M08 | 8 simultaneous 24-bit delta-sigma channels, PGA 1–128 per channel. inputs must stay below AVDD − 1.8 V at gain ≥ 8 |
| ADC clock | MCO1 from the MCU (PA8), 8 MHz | one crystal for the board, so ADC samples and timestamps cannot drift apart |
| sample rate | OSR 2048 → 1953 sps per channel | must be ≥ 500 sps; 3906 and 977 sps selectable in firmware |
| bridge excitation | LT3042 (or equivalent ≤ 1 µVrms LDO) at 2.49 V | fixed by the ADC input-common-mode limit. 0.1 % set resistor, ≤ 25 ppm/°C. firmware enable. ≥ 200 mA (eight 120 Ω bridges = 166 mA). current-limited on short |
| ADC reference | internal 1.2 V | optional unpopulated divider from the excitation to REFIN for ratiometric use |

## MCU: STM32F412CEU6 (UFQFPN48)

| function | peripheral | notes |
|---|---|---|
| bridge ADC | SPI + DRDY (EXTI) + SYNC/RESET | DMA on SPI. one DRDY interrupt per sample frame |
| ADC clock | MCO1 on PA8 | 8 MHz, from HSE |
| IMU | second SPI + INT | |
| microSD | SPI mode + card detect | 62 kB/s needed. SDIO may not be available on this package; use it only if CubeMX confirms the pins |
| USB | OTG FS, PA11/PA12, device only | data download, ROM DFU bootloader via a BOOT0 button |
| CAN | bxCAN | not on PA11/PA12 (taken by USB). PB8/PB9 expected; confirm the AF |
| aux + battery | ADC1, 5 channels | |
| excitation enable, shunt cal | 2 GPIO | |
| status | 2 LEDs | |
| debug | SWD (SWDIO, SWCLK, NRST), SWO optional | |
| expansion | UART + I2C on a header | GPS, radio, wheel speed |
| HSE | 8 MHz crystal | |

**pin budget:** the package has 36 I/O and the list above needs 37. pick one to cut: SWO, an LED, or the expansion I2C.

dev board for firmware before hardware exists: NUCLEO-F412ZG.

## other on-board parts

| block | part | requirement |
|---|---|---|
| IMU | TDK ICM-42688-P | measures chassis motion where the board is mounted |
| CAN transceiver | TI TCAN1462V-Q1 | ESD protection. 120 Ω termination on a solder jumper. powered from vehicle supply only |
| USB | USB-C receptacle | ESD array, 5.1 kΩ CC pull-downs |
| storage | microSD push-push socket | card detect |
| debug | Tag-Connect TC2030-NL | no fitted connector |

## power

```
vehicle 12 V → fuse (harness) → TVS → reverse-polarity protection → 5 V buck module ─┐
                                                                                     ├─ OR → 5 V → 3.3 V LDO (MCU, SD, IMU)
USB-C 5 V ───────────────────────────────────────────────────────────────────────────┘        → ferrite + low-noise LDO → 3.3 V analog (ADC AVDD)
                                                                                              → excitation LDO 2.49 V
```

- **input:** 9–16 V vehicle supply. survives reverse battery and load-dump-type spikes. the buck is rated ≥ 36 V.
- **USB only:** the board runs fully on USB alone (bench calibration, download), except CAN.
- **back-feed:** no back-feed between USB and the vehicle input.
- **noise isolation:** the switching regulator never directly feeds anything analog. linear regulators after it.
- **budget:** under 1.5 W from the vehicle with eight 350 Ω bridges excited.

## firmware behaviour the hardware has to allow

- **start of a run:** excitation on, wait 1 s to settle, run shunt cal on every channel, log the cal step, then start logging.
- **log contents:** 8 bridge channels, 4 aux, battery, IMU (6 axes), a sample counter and a timestamp. the actual sample rate goes in the file header, along with the run metadata: date, track or event, driver, tyre pressures, ride height, weather or ambient temperature, firmware version, and the board, sensor and calibration IDs.
- **between runs:** excitation off, so the gauges are unpowered.
- **CAN:** broadcast a sync or start/stop so several corner boards can be time-aligned. reception of start/stop from the car is optional.

## physical and environment

- **mounting:** on the chassis near the instrumented corner, inside a sealed enclosure. vibration, mud, water spray, −10 to +60 °C.
- **connectors:** must lock positively and be sealable through glands. JST PH, as on rev A, is fine for bench use only.
- **layout:**
  - the analog section is separated from the buck, SD and USB.
  - bridge input pairs are routed differentially and symmetrically.
  - the anti-alias resistors sit together so they share temperature.
  - analog and digital ground return paths are kept apart under the ADC.
- **size:** target under 100 × 80 mm.

## acceptance tests

| test | pass |
|---|---|
| inputs shorted, gain 128, 1953 sps | ≤ 1 µVrms per channel |
| excitation, eight 350 Ω dummy bridges | 2.49 V ± 0.5 %, settled < 1 s |
| shunt cal | −0.874 mV/V ± 1 % on each channel |
| one signal into all 8 channels | no inter-channel phase offset |
| 30 min logging to SD | zero dropped samples |
| 14.4 V on an aux signal pin | no damage, reading recovers |
| reversed vehicle supply | no damage, no current |
| USB-only power | all functions except CAN |

## open items

- confirm SDIO and CAN pin availability on UFQFPN48 in CubeMX before layout
- choose the sealed connector family
- confirm the LT3042 strapping (PGFB, ILIM) against the datasheet
- confirm the steel-compensated gauge part codes when ordering (CEA-06-125UT-350 is the baseline)
