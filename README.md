# Suspension strain-gauge logger

One-corner Baja suspension logger with **STM32F412CEU6**, ADS131M08, ICM-42688-P, microSD SPI, USB and classic bxCAN. The board started as a KiCad 10 import; the current circuit is defined by [string-gauge-daq.zen](string-gauge-daq.zen) and [spec.md](spec.md). The owner's brief is [board-spec.md](board-spec.md).

## Current architecture

- 8 simultaneous bridge channels; fixed2.49 V excitation, common shunt-calibration GPIO.
- 4 auxiliary ADC inputs plus vehicle monitor.
- PA8 MCO1 supplies8 MHz HSE to ADC; timestamps share the same crystal.
- SPI1 is shared by ADC and microSD with independent CS. IMU has dedicated SPI2.
- bxCAN1 is PB8/PB9 **AF8**. TCAN1462 VCC remains vehicle-only.
- SWD/NRST retained; optional SWO cut. No GPIO expander.
- Two LEDs and expansion UART+I²C retained. Former general-GPIO header contacts are reserved/NC.

The complete pin map, boot bias, SPI scheduling and power notes are in [docs/mcu-migration.md](docs/mcu-migration.md). The exact MCU registry package v0.1.0 is vendored. This migration requires new firmware and PCB rework; it is not a pin-compatible substitution for STM32F446RET6.

## Status and checks

The circuit has 256 physical components, including the 49-pad MCU and the four AUX fault-clamp Schottkys D10–D13. Zener build and native schematic/ERC are checked without electrical diagnostic suppressions. The PCB is placed, routed and poured (see [PCB layout](#pcb-layout)); KiCad DRC reports 0 errors and 0 unconnected items, with 7 accepted silkscreen-outline overlap warnings.

```sh
pcb sync
pcb build string-gauge-daq.zen
pcb bom -f json string-gauge-daq.zen
pcb apply schematic --no-open string-gauge-daq.zen
pcb apply layout --check --no-open string-gauge-daq.zen
```

BOM JSON includes DNP members: R78/R80 are intentionally **DNP**, not fitted, and must remain so for shared MOSI/BOOT1 reset behavior. Optional completion/reference parts retain their population flags (39 DNP members in total, including the 24 bridge-completion resistors, R87/R88 REFIN divider and inherited strap/option parts).

**Not yet release-ready:** layout is complete and DRC-clean, but the enclosure/mounting-hole pattern, sealed field-connector family and all bench performance/fault tests are still open (see spec blockers). The board is **99 × 76 mm** (target under 100 × 80 mm). The [Baja measurement decisions](docs/baja-measurement-decisions.md) select offline response correction of unchanged raw logs, and board/sensor calibration instead of an excitation trim circuit. The 500 Hz flatness target applies to validated processed data; the rail acceptance is now2.43–2.55 V, measured per board, rather than an unsupported±0.5% untrimmed guarantee. These are selected requirements/processes, not completed performance tests. No firmware or processing implementation is included.

Native supply-drive flags and unused-sheet-input no-connects are persistent KiCad annotations, not fake PCB parts. `pcb layout` synchronization removed the imported PCB groups again; all 19 groups (MCU, CH0–CH7, AUX1–AUX4, CAN, EXC, IMU, PWR with PWR.BUCK/PWR.LDO_3V3) were restored with their original footprint membership, and D10–D13 were added to AUX1–AUX4. Verify group preservation after any future layout synchronization.

## PCB layout

- **Outline / stack-up:** 99 × 76 mm, 4-layer 1.6 mm, ENIG, black mask. L1 signals + GND pour, **In1 solid GND**, **In2 power** split into EXC (2.49 V excitation, under the analog/bridge half) and V3V3 (digital, MCU/SD/USB/IMU/power half), L4 signals + GND pour. Stitching vias on a ~4 mm grid plus a 2.5 mm perimeter ring. JLCPCB standard rules: 0.1 mm track/space, 0.3/0.6 mm vias.
- **Mounting:** four plated M3 (DIN965) holes with GND rings at (3.2, 13.5), (95.8, 13.5), (3.2, 62.5), (95.8, 62.5) mm from the top-left corner, i.e. a **92.6 × 49.0 mm** pattern, each with a 3.5 mm-radius track/via keepout. This pattern is an assumption until the enclosure is chosen.
- **Floorplan:** field connectors on two edges only. Top: bridges CH1–CH4 (J5–J8), AUX1/AUX2 (J1/J2), vehicle power + CAN (J16). Bottom: bridges CH5–CH8 (J9–J12), AUX3/AUX4 (J3/J4), UART/I²C header (J13). USB-C (J15) and microSD (J17) are service connectors on the right edge. Left ~60 % is the analog section: bridge strips at both edges, input RC clusters around the ADS131M08, LT3042 excitation. Right: MCU, crystal, SD, USB, IMU, LEDs; power (TVS, reverse diode, TPSM33610S5-Q1 buck module, OR diodes) in the top-right corner, away from the ADC.
- **Routing:** bridge input pairs are tightly coupled, 45° and on L1 over GND. Channels 1, 3, 6 and 8 each have one single-conductor L4 crossover (~5–6 mm, two vias) before the input filter to fix connector-to-filter pin order. This is a known minor asymmetry; mirroring those filter clusters would remove it. USB D+/D− is a 90 Ω differential pair through the ESD array; CAN H/L is routed as a pair to the TVS/connector with JP13 termination near the transceiver. The SPI1 bus is shared by the ADC and SD with SD taps under the MCU; the IMU SPI2 pin order is mirror-reversed, so CLK/CS each use one short B.Cu hop. V5 runs as a 0.4–0.5 mm trace from the buck/OR node through the analog half to the LT3042 input, then along the bottom edge to the LDOs, aux 5 V jumpers, J13 and the USB OR diode (relocated to J13). About 182 mm to U5 gives roughly 0.2 Ω, or ~60 mV at 300 mA. The buck input ceramics C73/C75 sit ~6 mm from the module VIN pin; moving one beside VIN/GND would tighten the input loop.
- **Silkscreen:** reference designators hidden. Labels: CH1–CH8, AUX1–AUX4, PWR, CAN, USB, SD, SWD, UART/I2C, board name and REV B; footprint pin-1 marks retained.

[Original import repair evidence](docs/import-repairs.md) and [historical Rev-A part selection](docs/part-selection.md) remain available; their component counts precede this migration.

## Repository layout

| path | contents |
|---|---|
| `string-gauge-daq.zen`, `pcb.toml` | board definition (Zener) and workspace config |
| `modules/`, `components/` | per-part modules and component symbols/footprints used by the board |
| `vendor/` | vendored STM32F412CEU6 registry package |
| `eda/` | KiCad project, schematic and layout |
| `fab/rev-b/` | Rev B manufacturing package from commit 99bcdad: JLCPCB Gerber zip, BOM/CPL, schematic and assembly PDFs, DRC/ERC reports, STEP, IPC. See [fab/rev-b/README-fab.md](fab/rev-b/README-fab.md) |
| `board-spec.md`, `spec.md` | requirements brief and current design specification |
| `docs/` | test plan, MCU migration, measurement decisions, part selection, Baja Discord notes |
| `analysis/` | expected-load hand calcs (`python3 analysis/expected_loads.py`) and hardpoint-vs-sweep check (`python3 analysis/check_olin_geometry.py`) |
| `data/olin-baja/` | Olin Baja suspension workbook: hardpoints, global variables, toe/steer/Ackermann sweeps. Used by the analysis |
| `data/adams-mini-baja/` | Adams mini baja model export (reference; no longer used by the analysis) |
