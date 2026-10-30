# Suspension strain-gauge logger — design specification

## Purpose and source of requirements

One board per instrumented corner of a Baja double-wishbone suspension, rear corner first. Bond gauges to the wishbone arms; log arm loads, shock travel and chassis motion while driving. Fixture calibration mixes all bridge channels to correct directional cross-talk; results feed ANSYS stress/fatigue analysis.

This specification incorporates the owner's suspension-board brief and the required change to **STM32F412CEU6**. Measurement requirements follow [docs/test-plan.md](docs/test-plan.md). The referenced `suspension-load-daq/docs/design-notes.md` was not available; its contents have not been assumed.

## Bridge inputs and measurement requirements

All eight ADS131M08 channels acquire simultaneously. Per-channel multiplexing is prohibited.

| Channels | Sensor | Full-scale signal / range | Firmware PGA |
|---|---|---|---|
| 1–2 | LCA leg axial full bridges, Poisson | ±0.08 mV/V, ±60 µε | 128 |
| 3–4 | LCA leg bending full bridges | ±1.4 mV/V, ±1100 µε; stay on scale to about 2.7 mV/V yield | 32 |
| 5–6 | UCA leg axial full bridges | ±0.04 mV/V, ±30 µε | 128 |
| 7 | Toe-link axial bridge / instrumented rod end | ±0.5 kN | 32–128 |
| 8 | Shock inline load cell | 0–8 kN; typically 2 mV/V, verify load-cell data | 32 |

The legacy circuit uses CH0–CH7: these correspond to requirement channels 1–8 above.

- Full bridge is the default population. Half-bridge and three-wire quarter-bridge completion options remain, with completion resistors equal to gauge resistance.
- Nominal gauges are 350 Ω. 120 Ω gauges must also operate with an appropriate completion-resistor population and higher excitation current.
- Axial-channel noise requirement: ≤0.5 µV/V rms at about 2 ksps and gain 128, approximately 0.3 µε. The acceptance test below is the stricter ≤1 µVrms input-referred requirement.
- Input filter per channel: 1 kΩ + 1 kΩ, 0.1%, with 10 nF C0G differential capacitance and 1 nF C0G from each input to ground. Keep the resistor pair thermally close and layout symmetric.
- Gain flatness within 0.5% through 500 Hz applies to the **calibrated, offline-processed measurement chain**, not the uncompensated ADC output. Preserve raw samples and validate a common band-limited correction on all eight channels. Do not equalize near Nyquist or claim correction removes aliasing.
- A 100 kΩ shunt resistor, switched across one arm by a FET, on every channel. One direct MCU GPIO drives all eight FETs. Expected calibration step is approximately −0.874 mV/V with 350 Ω gauges and −0.300 mV/V with 120 Ω gauges; firmware gain-drift checks use the start/end ratio, so either works, but fixture/acceptance checks must use the value for the fitted gauge.
- Each bridge uses one locking six-contact connector: EXC+, EXC−, SIG+, SIG−, QTR and shield. Sealed connector family is not yet selected; installed JST PH is bench-only.

## Signal chain and excitation

- **ADS131M08:** eight simultaneous 24-bit delta-sigma channels, individually programmable PGA 1–128. Use the internal 1.2 V reference. R87/R88 remain unpopulated for the optional excitation-derived REFIN divider.
- **Clock:** MCU **PA8 MCO1**, sourced directly from the **8 MHz HSE crystal**. ADC clock and hardware timestamps share the same oscillator and cannot drift independently.
- **Default rate:** OSR 2048 gives exactly **1953.125 samples/s/channel** for nominal 8 MHz. OSR 1024 and 4096 give **3906.25** and **976.5625 samples/s/channel**. Log the actual selected rate in the file header. Minimum required rate is 500 samples/s/channel.
- **Excitation:** LT3042, nominal **2.49 V** from 100 µA × 24.9 kΩ. R59 is 0.1%, ≤25 ppm/°C. Firmware controls excitation enable; turn it off between runs. Noise target ≤1 µVrms, current capability ≥200 mA, short-circuit current limiting required.
- At 2.49 V, eight 350 Ω bridges draw **56.914 mA**; eight 120 Ω bridges draw **166 mA**. Verify current and LDO thermal margins at enclosure temperature.
- At PGA ≥8, keep ADC inputs below AVDD−1.8 V. A balanced 2.49 V bridge has 1.245 V common mode, leaving margin at the 3.3 V analog rail.
- **Strapping checked against LT3042 pin functions:** PG may float when unused; PGFB tied to IN disables unused power-good/fast-start functions; ILIM tied to GND uses internal backup current limiting. These existing connections are permitted. OUTS must Kelvin-sense the output, with SET-resistor return sensed at the load return.

## Four auxiliary analog inputs and battery monitor

- Aux inputs accept any three-wire ratiometric position sensor on MCU 12-bit ADC1: shock linear pot, Hall-effect arm-angle sensor (e.g. TI DRV5055, VDDA-supplied), rotary ride-height sensor or string pot. Linear pots are not rated for mud/dust; prefer sealed or contactless sensors for field runs. Quadrature/digital encoders are not supported on the aux inputs.
- Supply jumper selects VDDA (JP1–JP4 1-2, default) or 5 V (2-3) for active sensors such as brake pressure. The 5 V position is V5_BUCK (5.0 V, ahead of the D6/D7 OR diodes), not the 3.8–5.25 V V5 rail, so 5 V sensors see an in-spec supply. Like CAN, the 5 V sensor supply is therefore absent on USB-only power.
- Populate the divider option (R1/R3/R5/R7 = 16.9 kΩ with the 10 kΩ series resistor) for 0–5 V sensors so the MCU pin sees ≤3.2 V: 3.14 V at 5.00 V and 3.30 V at 5.25 V (ngspice). Protection includes 10 kΩ series resistance and must survive a 14.4 V signal-wire short with ≤5 mA injected into the pin, recover without damage, and retain ≥200 Hz bandwidth.
- Each AUX node has a BAT54WS-class Schottky (D10–D13) to V3V3 after the 10 kΩ series resistor. STM32F412 ADC pins are FT I/Os that allow no positive injection, so a 14.4 V wire short (~1.1 mA) is clamped near the 3.3 V rail: ngspice gives 3.49–3.67 V at the pin over −10 to +60 °C and typical/max BAT54WS VF, below the 4.0 V absolute maximum (DS Table 13). A lower-VF Schottky was rejected: e.g. CUS520 leaks ~2–5 µA at 3 V and 50–60 °C, a 20–50 mV error through 10 kΩ. V3V3 is used instead of VDDA so fault current does not pass through the FB1 VDDA filter; the clamp level is the same.
- Vehicle battery voltage is divided to a fifth ADC1 input.
- Analog inputs are not 5 V tolerant in analog mode. Existing protection/divider/filter populations still require test and worst-case review; choosing a jumper does not make an MCU ADC input 5 V tolerant.

## MCU and implemented allocation

**STM32F412CEU6**, UFQFPN48, exposed ground pad; 512 KiB flash, 256 KiB SRAM, up to 100 MHz. Use the exact CEU6 device, not a larger-pin-count substitute. The preferred development platform is NUCLEO-F412ZG; its pin routing is not the final board pinout.

The package's 36 GPIOs are fully assigned. **Optional SWO is cut** to retain both status LEDs and expansion I²C. No GPIO expander is installed. See `docs/mcu-migration.md` for the pin/AF table and firmware consequences.

| Function | Implemented choice |
|---|---|
| Bridge ADC | SPI1 + PC14 EXTI DRDY + PB2 SYNC/RESET; SPI DMA |
| ADC clock | PA8 MCO1, HSE /1 = 8 MHz |
| IMU | Dedicated SPI2 on PB12–PB15 + PC15 interrupt; INT2 unused |
| microSD | SPI mode, sharing SPI1 SCK/MISO/MOSI with ADC, independent PA10 CS; PC13 detect |
| USB | PA11/PA12 OTG FS, device-only; PA9 VBUS sense retained |
| CAN | bxCAN1 on **PB8/PB9 AF8**, not USB pins; classic CAN 2.0B, not FD |
| Aux/battery | ADC1 channels 0, 1, 4, 8, 9 |
| Excitation/shunt | Direct PB4 / PB3 outputs |
| Status | Two direct LED GPIOs, PB5 / PB10 |
| Debug | SWDIO, SWCLK, NRST via TC2030; SWO contact NC |
| Expansion | UART2 PA2/PA3 + I²C1 PB6/PB7 on existing header; former general-GPIO header contacts reserved/NC |
| HSE | One 8 MHz crystal; obsolete LSE crystal and its two capacitors removed |

PC13–PC15 are input-only in this allocation, respecting their backup-domain limits (≤2 MHz, ≤30 pF, no current sourcing). Keep IRQ traces light. The extra Rev-A user button and debounce capacitor are removed; BOOT0 button remains.

PB2 is also BOOT1: R64 holds it low during reset; R80's conflicting reset pull-up is DNP. Firmware starts MCO1 with ADC reset low for at least 2048 clock periods (256 µs), then releases reset and waits at least 5 µs. R78's ADC-MOSI pull-down is DNP because shared SD CMD/MOSI already has a pull-up.

## Other interfaces

- ICM-42688-P measures chassis motion at the mounted board. Firmware configures INT1 only, preferably latched, and disables unused INT2.
- TCAN1462V-Q1 with ESD protection and 120 Ω termination selected by solder jumper JP13. VCC comes only from vehicle-derived V5_BUCK; USB-only operation excludes CAN. STB is hard-strapped to normal-mode ground; TXD must idle high.
- USB-C receptacle, ESD array, separate 5.1 kΩ CC pull-downs. USB serves data download and intended ROM DFU entry through BOOT0. SWD is retained as the guaranteed programming/recovery interface; ROM DFU must be tested on the finished board.
- microSD push-push storage requested, with card detect and ≥62 kB/s sustained logging throughput. Confirm the existing socket's exact eject mechanism before field release; do not assume all microSD sockets are push-push.
- TC2030-NL is a PCB/pogo footprint, not a fitted connector.

## Power and budget

Vehicle 12 V → harness fuse → TVS / reverse-polarity protection → ≥36 V-rated 5 V buck → diode OR with USB-C 5 V → 5 V system rail. The OR diodes D6/D7 are RB168MM-40 low-leakage 40 V Schottkys; the V5 rail is therefore 3.8–5.25 V (USB 4.4 V minimum less about 0.5 V), and downstream LDO/LT3042 headroom is checked against that range. Linear regulation produces 3.3 V digital; ferrite plus low-noise LDO produces 3.3 V ADC analog; LT3042 produces 2.49 V excitation. Switching output must not directly feed analog circuitry.

- Vehicle operating input **9–16 V**. Survive reverse battery and specified load-dump-type spikes without damage. A concrete transient waveform/source impedance is still needed for an energy qualification.
- USB-only runs all measurement, storage and download functions except CAN and the 5 V aux sensor supply (both come from V5_BUCK). No USB-to-vehicle or vehicle-to-USB back-feed.
- Vehicle input power target **<1.5 W with eight 350 Ω bridges excited**. This is an average operating budget, not permission to ignore SD write peaks; hardware/current measurements are required.
- Preserve continuous local ground reference and segregate noisy return currents. “Analog/digital return paths apart under ADC” means controlled return-current placement and a local common reference, not an arbitrary split/slot beneath input pairs or clocks.

## Firmware behavior and data

1. Start run: enable excitation, wait 1 s, switch all shunt calibration FETs together, log each channel's calibration response, then acquire the run.
2. Log eight bridge channels, four aux channels, battery, six-axis IMU, sample counter and hardware timestamp. Include actual rate/gains/configuration in the header, plus run metadata: date, track/event, driver, tyre pressures, ride height, weather/ambient temperature, firmware version, and board/sensor/calibration IDs.
3. At run end, stop the motion/acquisition test, record a second shunt on/off step while excitation remains enabled, then disable excitation. Compare start/end shunt increments to detect gain drift. Do not zero real static suspension preload when taking either shunt step.
4. CAN broadcasts sync/start/stop for multiple corner boards; vehicle-command reception is optional.
5. Share SPI1 deliberately: ADC uses mode1; SD uses mode0. Keep independent CS high when switching configuration, wait for bus idle/MISO release, buffer SD writes, and schedule ADC read deadlines. Independent SPI2 avoids motion-sensor contention.
6. All stated performance tests require actual firmware and hardware; schematic build/ERC do not demonstrate acquisition performance.

## Mechanical and environment

- **Target strictly under 100 × 80 mm**, mounted on chassis near the instrumented corner inside a sealed enclosure.
- Vibration, mud and water spray; ambient −10 to +60 °C.
- Positively locking, gland/seal-compatible field connectors. JST PH remains a bench-only interim population; sealed family is open.
- Separate analog section from buck, SD and USB. Route bridge pairs differentially/symmetrically; pair input resistors thermally. Place supply bypasses locally, with short complete return loops.
- Board outline 99 × 76 mm. Four-layer 1.6 mm ENIG/black mask: L1 signal + GND pour, In1 solid GND, In2 power (EXC region under the analog half, V3V3 elsewhere), L4 signal + GND pour; JLCPCB standard 0.1 mm track/space and 0.3/0.6 mm vias.
- Four plated M3 holes with GND rings and 3.5 mm keepouts on a 92.6 × 49.0 mm pattern (corners at 3.2/95.8 mm × 13.5/62.5 mm). The hole pattern is a layout assumption pending the enclosure.
- Field connectors occupy only the top and bottom edges (bridges, AUX, power/CAN, UART/I²C). USB-C and microSD are service connectors on the right edge. JST PH footprints are retained as the bench population.

## Acceptance tests (requirements, not completed results)

| Test | Pass criterion |
|---|---|
| Inputs shorted, PGA128, 1953.125 sps | ≤1 µVrms per channel |
| Excitation with eight 350 Ω dummy bridges | 2.49 V nominal; **2.43–2.55 V electrical acceptance**, measured/recorded per board, settled in <1 s |
| Corrected frequency response, all bridge channels | Within 0.5% through 500 Hz, verified by an injected-signal sweep; record corrected noise and phase/delay |
| Shunt calibration | −0.874 mV/V ±1% on every channel (350 Ω gauges; −0.300 mV/V ±1% with 120 Ω) |
| One common signal to all channels | No inter-channel phase offset beyond component tolerance. The input RC adds ~24.7 µs group delay (350 Ω bridge); 5% C0G capacitors allow about ±1.2 µs (±0.2° at 500 Hz) channel-to-channel. Measure and correct offline if it matters |
| Logging for 30 minutes | Zero dropped samples |
| Aux signal short to 14.4 V | No damage, reading recovers |
| Reversed vehicle input | No damage / no operating current; define leakage measurement threshold |
| USB-only | All functions except CAN and the 5 V aux sensor supply |

## Resolved Baja measurement decisions

The owner delegated these two choices for a student Baja car. The priority is reliable raw logging, reproducible fixture calibration and useful offline force/fatigue data, rather than adding precision hardware or real-time DSP that the application does not need. Details and evidence are in [docs/baja-measurement-decisions.md](docs/baja-measurement-decisions.md).

1. **Frequency response: retain raw 1953.125 sps logging and correct offline.** Do not add MCU equalization or raise the default acquisition rate. Preserve ADC counts unchanged; apply one documented, band-limited response correction identically to all bridge channels before the cross-talk/force reconstruction. The 0.5% flatness criterion is for this processed chain and must be demonstrated by a sweep, not inferred from a theoretical inverse alone. Keep raw and processed products separate, align fixed filter delays with aux/IMU timestamps, and evaluate aliasing/impacts in validation. Hardware aliasing, saturation and dynamic noise are not repaired by equalization.

   **Correction to the earlier estimate:** ADS131M08 OSR2048 is sinc³ at OSR1024 followed by a sinc¹ average of two—not a sinc³ at OSR2048. At 8 MHz CLKIN, the modeled digital gain at 500 Hz is about 0.84838; The specified RC must be modelled with the bridge's source impedance (each output leg is R/2): for 350 Ω bridges the RC pole is ~6.45 kHz, giving **0.84584 (15.42% droop)** and about **1.1823× / +1.454 dB** correction at 500 Hz; for 120 Ω bridges the pole is ~7.15 kHz, giving 0.84631 and 1.1816×. The earlier 0.84654 / 1.1813× figure ignored the bridge resistance. The earlier 28.31% estimate was incorrect. Other OSRs need their own correct model and coefficient set.

2. **Excitation: use measurement-chain calibration, not a trim pot or new precision reference.** Keep LT3042, the fixed 24.9 kΩ 0.1% low-TCR resistor and internal ADC reference. Replace the unnecessarily tight absolute-rail ±0.5% acceptance with the safe loaded **2.43–2.55 V** window and measure/record actual excitation with an accurate DMM during board/fixture calibration. At 2.55 V the eight 120 Ω bridges draw 170 mA and balanced common mode is 1.275 V, below AVDD(min)−1.8 V = 1.434 V. Current/thermal/noise tests still apply.

   Fixture calibration must include the actual board, harness and sensor assembly. Use start/end **shunt increments** to normalize/check gain against the fixture baseline; a >0.5% change is a run-quality flag requiring investigation, not permission to silently rescale every error. Calibration cannot remove excitation noise, gauge thermal apparent strain, lead/bond changes or real preload. There is no claim of ±0.5% untrimmed rail accuracy or guaranteed overall force accuracy. This decision changes the original rail-accuracy acceptance; it does not relax the noise or force-calibration obligations.

## Remaining implementation / qualification blockers
1. Choose the sealed connector family and enclosure, and confirm or adjust the assumed 92.6 × 49.0 mm M3 hole pattern. The PCB is now placed, routed, poured and DRC-clean within 99 × 76 mm, but a connector-family change would require re-placement of the edge connectors; no manufacturing-ready claim.
2. Qualify the selected offline correction/calibration process, power budget, transients, reverse-input leakage, auxiliary fault recovery, noise and 30-minute logging on actual hardware. Load-cell channel8 gain/range requires its datasheet.
3. Confirm actual SPI/AF configuration in CubeMX or equivalent device data before firmware/layout release. SDIO is not used, so its alternative pins need not be committed. bxCAN1 PB8/PB9 is verified from DS11139 Table11 as AF8.
4. LT3042 ILIM/PGFB strapping is checked as allowed; physical Kelvin layout and thermal/short-circuit tests remain mandatory. Confirm steel-compensated gauge ordering codes; CEA-06-125UT-350 is the baseline, not a validated substitution list.
5. Supply the referenced design-notes document for a line-by-line Rev-A comparison. Bare imported schematic symbol-library IDs still generate advisory link warnings; no electrical diagnostics are suppressed.
