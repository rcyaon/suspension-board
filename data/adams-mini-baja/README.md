# mini baja model data for testing

what the csv files in this folder are, why they are here, and how they help test the board and the load path it measures.

status: the files are a straight export of the adams car database `mini_baja.cdb` (saved 2026-04-13). nothing was simulated. they hold model inputs, not adams results and not measurements from the car.

## short answer

the board measures forces in a wishbone. a measurement is only testable if there is something to compare it to. these files are that something: the vehicle model's geometry, masses, spring and damper curves, tires and test events, in a form any script can read without adams.

they let you work out what the arm should see before the car runs, and check what the board logged after.

## why csv

- **no adams needed.** adams car only runs on windows and linux x86, and needs a licence. the database is plain text but in adams' own block format. csv opens in python, excel, matlab or a text editor on any machine.
- **one table per kind of data.** every hardpoint is in one file, every curve in another. the database spreads them over 50-odd files in a dozen folders.
- **diffable.** the files are text in git. if the model changes and the export is rerun, the diff shows which hardpoint or spring rate moved.
- **calculated columns are plain numbers.** masses and engine power are already worked out, so nothing depends on a spreadsheet recalculating.

## files

| file | what is in it | what testing uses it for |
|---|---|---|
| `mini_baja_hardpoints.csv` | x, y, z of every suspension, steering, frame and powertrain point (mm, left side) | arm geometry: arm length, shock angle, motion ratio, the arm frame the gauges measure in |
| `mini_baja_parts.csv` | mass, centre of mass and inertia of every part and wheel | static corner loads and unsprung mass |
| `mini_baja_assemblies.csv` | which subsystems make up each vehicle, with mass per subsystem and total | which variant the numbers belong to (swingarm rear or double-wishbone rear) |
| `mini_baja_curves.csv` | force tables for springs, dampers, bump stops, bushings, differential, steering assist | expected shock force at a given travel and velocity |
| `mini_baja_components.csv` | each spring, damper, bump stop and bushing, the property file it uses, preload and clearance | ties a curve to a place on the car; spring preload and bump stop clearance |
| `mini_baja_parameters.csv` | brake, powertrain, steering and alignment settings | braking and drive torque that reach the arms; toe and camber settings |
| `mini_baja_tires.csv` | the four tire property files side by side | tire vertical stiffness and grip limits, which cap the force the contact patch can make |
| `mini_baja_engine_map.csv` | engine torque and power against rpm | upper bound on drive force |
| `mini_baja_property_values.csv` | single values from property, road and driver files: units, spring free lengths, bushing damping, event speeds | units for every curve; the road and speed of each model event |

## why this works for testing

### 1. it sets the expected range before anything is built

bridge gain, adc range and gauge placement all depend on how big the forces are. the model gives a first number without the car:

- static corner load from the masses in `mini_baja_parts.csv`
- shock force through travel from the spring curve plus preload (front spring is 35 lbf/in, about 6.1 N/mm, with 1000 N preload)
- shock force on a hit from the damper curve at a given shaft speed
- the ceiling from tire grip and the bump stop curve

if the board is ranged for those and the first log sits far outside them, either the board or the model is wrong, and that is found on day one.

### 2. it gives the geometry that turns a shock force into an arm force

the gauges measure in the arm frame. `mini_baja_hardpoints.csv` has the inner pivots, the outer ball joint and the shock mount for both arms, which is what is needed to resolve a force at the ball joint into the along-arm, fore-aft and normal channels used in `../double-wishbone-fatigue.md`.

in this model the front shock mounts to the lower arm (`lwr_strut_mount` sits on `lower_control_arm`), so the lower arm is the bending arm and the upper arm is the mostly axial one.

### 3. it gives a prediction to check the calibration against

fixture calibration turns strain into force. the check on that calibration is a known situation on the car: sitting at ride height, the arm force should match the static load worked out from the model masses and geometry. a mismatch there points at the calibration matrix or at a wrong mass in the model, before any fatigue number is trusted.

### 4. it defines repeatable test events

the database has roads (flat, 8 inch half-round, big bump, gravel) and driver events (constant 15 mph, 25 mph acceleration, constant throttle). driving the real car over the same obstacle at the same speed gives a logged run that can be laid over the adams run of that event, channel by channel, once the model is run.

### 5. it is checkable without trusting the export

every row names the source file it came from. any value can be looked up in the original database in a text editor.

## what these files do not give

- **no loads.** there are no adams results here. predicted arm forces over a bump need the model run in adams car. until then, the checks above are hand calcs from the inputs.
- **not the real car.** the model is only as close to the car as whoever built it made it. the two double-wishbone subsystems each carry a 50 kg `subframe`, which looks like a template default, not a baja part. measure the car's corner weights before using the model masses for anything.
- **mixed units.** subsystems are in mm, N, kg. the baja spring and damper files are in inch and pound_force. the unit columns in `mini_baja_curves.csv` say which; convert before combining.
- **left side only.** hardpoints with `left/right` symmetry are the left side. mirror y for the right.
- **not exported:** template topology, frame and swingarm graphics, road profile point tables, the xml event files.

## regenerating

the export came from `mini_baja.cdb` through a one-off script that was not saved in this repo. if the model changes, the export has to be redone from the new database, and the diff reviewed.
