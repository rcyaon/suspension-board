# Suspension strain logger REV B: fabrication and assembly package

Source: `code.diode.computer/lena/b/string-gauge-daq`, branch `main`, commit **99bcdad**.
Exported with KiCad 10.0.7 (`kicad-cli`) from `eda/suspension-load-daq.kicad_pcb` / `.kicad_sch`.
Status: prototype/bench build. The enclosure hole pattern, sealed field connectors and all bench
performance/fault tests are still open (see `spec.md` blockers in the repo).

## 1. PCB fabrication (JLCPCB)

Upload **`suspension-load-daq_revB_99bcdad_gerbers.zip`** (same files as `gerbers/`).

| Item | Value |
|---|---|
| Size | 99.0 x 76.0 mm, rectangular |
| Layers | 4, 1.6 mm FR-4 |
| Layer order | GTL `F.Cu` (signal + GND pour), G1 `In1.Cu` (solid GND), G2 `In2.Cu` (power: EXC 2.49 V left / V3V3 right), GBL `B.Cu` (signal + GND pour) |
| Copper | 1 oz outer / 0.5 oz inner (standard) |
| Impedance control | Not required (USB full-speed, CAN). Optional: JLC04161H-7628 for the 90 ohm USB pair |
| Finish / mask / silk | ENIG / black / white |
| Min track / space | 0.10 / 0.10 mm |
| Min via | 0.30 mm drill / 0.60 mm pad, tented, not filled (QFN exposed-pad thermal vias under U1/U2/U3 are unfilled) |
| Holes | 673 PTH: 591 x 0.30 vias, 4 x 0.60 plated slots (USB-C shield), 64 x 0.75, 10 x 1.00, 4 x 3.20 (M3). 5 NPTH: 2 x 0.65, 3 x 0.99 |
| Files | RS-274X, 4.6 mm, Protel extensions, mask subtracted from silk; Excellon mm, PTH/NPTH separate; Gerber job file included |
| Order number | No JLC order-number placeholder on the silk: choose "remove" or accept default placement |

## 2. Assembly (JLCPCB), top side only

- BOM: `assembly/suspension-load-daq-BOM-JLCPCB.csv`. 63 lines, 192 parts, DNP excluded; LCSC numbers taken from catalog offers at export time.
- CPL: `assembly/suspension-load-daq-CPL-JLCPCB.csv`. 192 placements in mm, absolute KiCad coordinates (same origin as the Gerbers).
- Through-hole parts: J1-J13 and J16 (JST PH bench connectors, 2x5 header). Use THT assembly or hand-solder.
- Not assembled: 39 DNP parts (`assembly/suspension-load-daq-DNP.txt`), test points TP1-TP7, solder jumpers JP1-JP13, TC2030 pad footprint J14, mounting holes H1-H4.
- Solder-jumper defaults: JP1-JP4 are bridged 1-2 in copper (AUX sensor supply = VDDA). JP5-JP12 and JP13 (CAN 120 ohm termination) are open.
- **Check rotations in JLC's placement preview.** KiCad and JLC zero orientations differ for some packages (SOT-23/SOD, QFN pin 1, connectors).

### Sourcing gaps at export time
- C13, C17, C21, C25, C29, C33, C37, C41: Murata GCM1885C2A103JE02W (10 nF C0G 100 V 0603) has **no LCSC offer**. Parametric equivalents with LCSC offers: TDK CGA3E1C0G2A103J080AC (LCSC C432938) or TDK CGA3EAC0G2A103J080AC (LCSC C342796). Use only with owner approval.
- Zero LCSC stock: C73/C75 TDK C3225X7R1H475K250AB (C2183952), IC1 ICM-42688-P (C20619370), U4 TPSM33610S5QRDNRQ1 (C51957001). Use JLC global sourcing, consignment, or approved alternates.

## 3. Verification at export

- KiCad DRC with zones refilled: **0 errors, 0 unconnected**; 7 cosmetic silk-outline overlaps (JP3/J3, JP4/J4, D2/C54). See `docs/suspension-load-daq-drc.rpt`.
- Schematic ERC: 0 errors; 542 warnings, 510 of them inherited symbol-library advisories (`docs/suspension-load-daq-erc.rpt`).
- Gerbers re-rendered from the fab files (`pcb gerber render`): finished top view, solid In1 GND and the In2 EXC/V3V3 split checked.
- Drill counts match the board (591 vias, 14 THT connectors, 4 M3 holes).
- BOM and CPL designator sets are identical (192 parts).

## 4. Contents

| Folder | Files |
|---|---|
| `gerbers/` | Gerbers (GTL, G1, G2, GBL, GTP, GBP, GTO, GBO, GTS, GBS, GM1), PTH/NPTH Excellon, `.gbrjob` |
| `assembly/` | JLC BOM and CPL, full BOM (all 231 BOM parts with DNP flags, LCSC/DigiKey/Mouser numbers), KiCad position file, DNP list |
| `docs/` | Schematic PDF, top assembly drawing (DNP crossed out), layer PDF (F.Cu, In1, In2, B.Cu, silk, mask), drill maps and report, DRC/ERC reports |
| `3d/` | STEP model (DNP excluded) for enclosure work |
| `ipc/` | IPC-2581 and IPC-D-356 netlist |
| `renders/` | Top/bottom 3D renders |
