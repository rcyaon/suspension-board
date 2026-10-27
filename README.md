# Baja Suspension Load Logger (Rev B)

A data logger that mounts on one corner of the Baja car and records the forces going through the suspension arms while the car drives. Today the arm design rests on estimated loads ("3 g bump", "5 g landing"). This board replaces those estimates with measured ones, which then feed the stress and fatigue analysis.

![Rev B board, top](fab/rev-b/renders/suspension-load-daq-top.png)

## Status

- **Board design:** finished. The manufacturing files are ready to send to JLCPCB.
- **Firmware:** written and passing all tests in a computer simulation of the board, driven by the Adams mini baja model.
- **Still open:** the firmware has not run on a real board yet. Bench tests (noise, long logging runs, fault tests), sealed connectors and the enclosure still need to be done.

## What to look at

| To check… | Open |
|---|---|
| What the board has to do | [Design specification](spec.md) |
| Whether the firmware works | [Firmware test results](firmware/test/results/summary.md) |
| The circuit | [Schematic (PDF)](fab/rev-b/docs/suspension-load-daq-schematic.pdf) |
| The board layout | [Layout (PDF)](fab/rev-b/docs/suspension-load-daq-layers.pdf), [top](fab/rev-b/renders/suspension-load-daq-top.png) and [bottom](fab/rev-b/renders/suspension-load-daq-bottom.png) renders |
| The parts list | [Bill of materials (CSV)](fab/rev-b/assembly/suspension-load-daq-BOM-full.csv) |
| What's needed to order boards | [Fabrication package](fab/rev-b/README-fab.md) |

## What it measures

| Where | What | Sensors |
|---|---|---|
| Lower control arm | Push/pull and bending (it carries the shock) | 4 strain gauges |
| Upper control arm | Push/pull and fore-aft | 2 strain gauges |
| Toe link | Push/pull | 1 strain gauge |
| Shock | Force through the shock, used as a cross-check | 1 load cell |
| Shock travel and extras | Position sensors | 4 inputs |
| Chassis motion | Acceleration and rotation | Built-in motion sensor |

## How it works

- All 8 force channels are recorded at the same moment, nearly 2,000 times per second, to a microSD card.
- Each run checks its own calibration at the start and end, and flags the data if the readings drifted.
- One board can start and stop the others, and they all record a shared timing signal, so all four corners can be compared side by side.
- The card holds the raw readings. They are turned into forces afterwards on a computer, using a calibration done on each arm before it goes on the car.

---

Everything else in this repo is design source or tooling, and you don't need it to review the board. Firmware build instructions are in [firmware/README.md](firmware/README.md).
