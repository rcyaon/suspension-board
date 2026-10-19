# olin baja suspension sheets

the olin baja sae suspension workbook, one csv per tab, plus the workbook and its pdf print in `source/`. the csvs are copied byte for byte from the tab exports and only renamed.

| file | tab | contents |
|---|---|---|
| `olin_baja_global_variables.csv` | GLOBAL VARIBLES | mass, weight split, cg height, wheelbase, brake/drive split, unsprung masses, sweep settings |
| `olin_baja_front_hardpoints_mm.csv` | FRONT SUSPENSION (MM) | 18 front points, mm. the analysis reads this one |
| `olin_baja_front_hardpoints_in.csv` | FRONT SUSPENSION (IN) | same points in inches |
| `olin_baja_rear_hardpoints_mm.csv` | REAR SUSPENSION (mm) | 18 rear points, mm. the header says `(in)` but the values are mm |
| `olin_baja_rear_hardpoints_in.csv` | REAR SUSPENSION (in) | same points in inches |
| `olin_baja_front_toe_vs_travel.csv` | TOE ANGLE THROUGH TRAVEL | front sae toe (deg) against wheel travel (mm, + = bump), ±228 mm |
| `olin_baja_steer_angle_vs_rack.csv` | STEERING ANGLE THROUGHT STEERIN | front -y wheel angle (deg) against rack travel (mm), ±56 mm. no header row |
| `olin_baja_ackermann_vs_rack.csv` | ACKERMANN THROUGH STEERING RACK | ackermann (%) against rack travel (mm) |
| `source/olin_baja_suspension.xlsx`, `.pdf` | all | the original workbook and its print |

mm and inch sheets agree to 0.013 mm, so the inch files are only a cross-check.

## conventions

- x rearward, z up. the front sheet is on the -y side, the rear sheet on +y. `expected_loads.py` mirrors the rear to -y so both are the left corner.
- **front and rear use different origins.** the wheel centres are 2651 mm apart in x where the wheelbase is 1500 mm, and their heights differ (-39 mm front, 295 mm rear). never mix front and rear points in one calculation.
- the front and rear sheets list the same 18 points in the same order under different names, so the analysis maps them by row number. rows 11/12 repeat the damper ends. rows 15–18 (part cgs) are not used; the front cgs are at x ≈ 4000 mm, in yet another frame.
- both dampers mount on the lower arm (22–32 mm off the arm plane, a tab offset), so the lower arm is the bending arm.

## checks

`python3 analysis/check_olin_geometry.py` works out the static toe, bump steer and steering ratio from the hardpoints and compares them with the sweep sheets. at ride height they agree: static toe -0.616° exact, bump steer within 4 %, steering ratio within 0.5 %. so these hardpoints are the ones the sweeps were made from. +rack travel moves the inner tie rod inboard.

## what the sheets do not give

- **tire radius.** the analysis assumes 295 mm, the rear wheel-centre height, on the guess that the rear frame's z = 0 is the ground. confirm against the tire size.
- **driver.** the sheet does not say whether 163.3 kg includes the driver. if it does not, set `DRIVER_KG` in the script.
- **track.** the hardpoints give 1593 mm front and 1377 mm rear. a 1593 mm front track plus tire width is wider than the baja 64 in (1626 mm) limit, so the front frame's y = 0 may not be the centreline. track only affects lateral load transfer.
- **springs, dampers, roll stiffness.** none. lateral load transfer is split by axle mass.
- the ackermann sheet is stored but not checked: its definition of ackermann % is not stated.
