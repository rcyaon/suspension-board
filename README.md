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

The circuit has252 physical components, including the new49-pad MCU. Zener build and native schematic/ERC are checked without electrical diagnostic suppressions. Original unrelated placements,50 tracks,21 vias and19 group containers are preserved; U3 and its group reflect the replacement and five removed obsolete parts.

```sh
pcb sync
pcb build string-gauge-daq.zen
pcb bom -f json string-gauge-daq.zen
pcb apply schematic --no-open string-gauge-daq.zen
pcb apply layout --check --no-open string-gauge-daq.zen
```

BOM JSON includes DNP members: R78/R80 are intentionally **DNP**, not fitted, and must remain so for shared MOSI/BOOT1 reset behavior. Optional completion/reference parts retain their population flags.

**Not manufacturing-ready:** final placement, outline/planes/routing, sealed connectors and performance/fault tests remain incomplete. Target size is **under100×80 mm**. The [Baja measurement decisions](docs/baja-measurement-decisions.md) select offline response correction of unchanged raw logs, and board/sensor calibration instead of an excitation trim circuit. The 500 Hz flatness target applies to validated processed data; the rail acceptance is now2.43–2.55 V, measured per board, rather than an unsupported±0.5% untrimmed guarantee. These are selected requirements/processes, not completed performance tests. No firmware or processing implementation is included.

Native supply-drive flags and unused-sheet-input no-connects are persistent KiCad annotations, not fake PCB parts. pcb0.4.85 still removed imported PCB groups during synchronization; those were restored through live IPC. Verify group preservation after future layout synchronization.

[Historical Rev-A part selection](docs/part-selection.md) remains available; its component counts precede this migration.

## Repository layout

| path | contents |
|---|---|
| `string-gauge-daq.zen`, `pcb.toml` | board definition (Zener) and workspace config |
| `modules/`, `components/` | per-part modules and component symbols/footprints used by the board |
| `vendor/` | vendored STM32F412CEU6 registry package |
| `eda/` | KiCad project, schematic and layout |
| `board-spec.md`, `spec.md` | requirements brief and current design specification |
| `docs/` | test plan, MCU migration, measurement decisions, part selection, Baja Discord notes |
| `analysis/` | expected-load hand calcs (`python3 analysis/expected_loads.py`) |
| `data/adams-mini-baja/` | Adams model export used by the analysis |
