# Firmware test results

**All 3 test scenarios pass** (78 checks). Run `make -C firmware test` to regenerate this page.

The real firmware from `src/` runs on a PC, connected to simulated versions of the board's chips:
the ADS131M08 ADC, the microSD card and the ICM-42688 IMU. They are fed a 10 s drive over a bump,
whoops, a 0.5 m drop and braking, generated from the Adams mini baja rear-corner model at 15 mph.
The SD card file is then decoded by separate code and every sample is compared with that input.

## What the tests show

- **No data lost.** A normal run logged all 30616 samples over 15.7 s with zero missing. At double the sample rate it logged all 61231 samples, also with zero missing.
- **Measurements are accurate.** All 8 strain-gauge channels match the input to 0.35 µV RMS, which is the simulator's own noise level, so the firmware adds no error.
- **Shock force is right.** Peak landing force logged 4930 N against 4929 N in the Adams model.
- **Timing is precise.** Samples arrive every 512 µs within ±2 µs, and each timestamp is within 1.7 µs of the moment the ADC sampled.
- **Saving to the SD card never gets in the way of measuring.** During the run the SD card froze 2 times for 120 ms each. The RAM buffer peaked at only 29 of 256 blocks and no reading was missed.
- **Calibration works without manual tuning.** The built-in shunt calibration measured the sensor supply as 2.487 V (actual 2.487 V), so no trim pot is needed.
- **Losses are reported, not hidden.** When the card froze for 1.5 s, longer than the RAM buffer can cover, the firmware lost 888 samples. That count was the same in the data, the file trailer and the firmware's own count, and each loss was marked in the log. It then caught up without missing another reading.

![shock force: logged vs input](shock_force.svg)

![strain channels: logged vs input](strain_channels.svg)

![error added per channel](channel_error.svg)

![time between samples](sample_timing.svg)

## What these tests don't cover

This is a simulation, so it says nothing about analog noise, real SD card timing, the ADC's filter delay (corrected afterwards on the PC) or the PCB itself. Those need the bench tests in `spec.md` and `docs/test-plan.md`.

## Detailed checks

### Normal drive: PASS

| check | result | detail |
|---|---|---|
| no clashes on the shared SPI bus | pass | 0 errors |
| ADC always driven in the correct SPI mode | pass | 0 errors |
| no corrupted ADC readings | pass | 0 errors |
| no SD card command errors | pass | 0 errors |
| SD card always driven in the correct SPI mode | pass | 0 errors |
| no IMU errors | pass | 0 errors |
| every ADC reading collected before the next one replaced it | pass | 0 |
| log file created on the SD card | pass | CONFIG.TXT, RUN00001.BIN |
| file stored in one unbroken piece, card file table consistent | pass | 3853 clusters |
| unused reserved space returned to the card when the file closes | pass | file 1972736 B, 3853 clusters |
| file header records sample rate, gains and run settings | pass | 1953.125 sps, gains [128, 128, 32, 32, 128, 128, 64, 32] |
| no missing or out-of-order data blocks | pass | 3845 blocks |
| no samples lost | pass | 30616 samples, 0 missing |
| samples evenly spaced every 512 µs (within ±5 µs) | pass | 510..514 us |
| sample count in the file trailer matches the data | pass | 30616 |
| each timestamp within 5 µs of the moment the ADC sampled | pass | -0.3..1.7 us |
| all 8 strain-gauge channels match the input to within simulator noise | pass | residual rms 0.35-0.35 uV input-referred, max 1.64 uV (sim noise 0.35 uV) |
| built-in calibration reads the expected -0.874 mV/V (±1 %) on every channel | pass | start -0.8724..-0.8724 mV/V |
| end-of-run calibration within 0.5 % of start (no gain drift) | pass | max 11 ppm |
| shock force from the load cell (CH8) matches the Adams model | pass | rms error 0.57 N, peak 4930 N (truth 4929 N) |
| shock travel sensor (AUX1) matches within 1.5 ADC counts | pass | max 0.60 LSB |
| IMU vertical acceleration matches within 1 mg | pass | max 0.24 mg |
| fresh IMU reading on ≥ 95 % of frames where one is possible (100 % of frames at this rate) | pass | 100.0 % fresh |
| run follows the set order: 1 s settle, 0.5 s calibration at start and end, sensors powered off at the end | pass | settle 1.000 s, shunt on 0.500, 0.500 s |
| CAN bus: start/stop sent, 10 Hz time sync logged, other board's sync received | pass | 156 sync tx events, rx from node [2] |
| writing to the SD card never made the firmware miss an ADC reading | pass | 0 frames read right after an SD step, ring max 29/256 blocks, 2 card stalls |

### SD card freezes for 1.5 s (longer than the ~1 s RAM buffer): PASS

| check | result | detail |
|---|---|---|
| no clashes on the shared SPI bus | pass | 0 errors |
| ADC always driven in the correct SPI mode | pass | 0 errors |
| no corrupted ADC readings | pass | 0 errors |
| no SD card command errors | pass | 0 errors |
| SD card always driven in the correct SPI mode | pass | 0 errors |
| no IMU errors | pass | 0 errors |
| log file created on the SD card | pass | CONFIG.TXT, RUN00001.BIN |
| file stored in one unbroken piece, card file table consistent | pass | 3741 clusters |
| unused reserved space returned to the card when the file closes | pass | file 1915392 B, 3741 clusters |
| file header records sample rate, gains and run settings | pass | 1953.125 sps, gains [128, 128, 32, 32, 128, 128, 64, 32] |
| no missing or out-of-order data blocks | pass | 3733 blocks |
| lost samples counted the same way in the data, the file trailer and the firmware | pass | gaps 888, trailer 888, firmware 888 |
| every loss is marked in the log | pass | 29 events |
| samples evenly spaced except where lost (within ±5 µs) | pass | 510..514 us |
| sample count in the file trailer matches the data | pass | 30616 |
| each timestamp within 5 µs of the moment the ADC sampled | pass | -0.3..1.7 us |
| all 8 strain-gauge channels match the input to within simulator noise | pass | residual rms 0.35-0.35 uV input-referred, max 1.52 uV (sim noise 0.35 uV) |
| built-in calibration reads the expected -0.874 mV/V (±1 %) on every channel | pass | start -0.8724..-0.8724 mV/V |
| end-of-run calibration within 0.5 % of start (no gain drift) | pass | max 11 ppm |
| shock force from the load cell (CH8) matches the Adams model | pass | rms error 0.57 N, peak 4930 N (truth 4929 N) |
| shock travel sensor (AUX1) matches within 1.5 ADC counts | pass | max 1.43 LSB |
| IMU vertical acceleration matches within 1 mg | pass | max 0.24 mg |
| fresh IMU reading on ≥ 95 % of frames where one is possible (100 % of frames at this rate) | pass | 100.0 % fresh |
| run follows the set order: 1 s settle, 0.5 s calibration at start and end, sensors powered off at the end | pass | settle 1.000 s, shunt on 0.500, 0.500 s |
| CAN bus: start/stop sent, 10 Hz time sync logged, other board's sync received | pass | 145 sync tx events, rx from node [2] |
| writing to the SD card never made the firmware miss an ADC reading | pass | 86 frames read right after an SD step, ring max 256/256 blocks, 3 card stalls |

### Double sample rate (3906 samples/s): PASS

| check | result | detail |
|---|---|---|
| no clashes on the shared SPI bus | pass | 0 errors |
| ADC always driven in the correct SPI mode | pass | 0 errors |
| no corrupted ADC readings | pass | 0 errors |
| no SD card command errors | pass | 0 errors |
| SD card always driven in the correct SPI mode | pass | 0 errors |
| no IMU errors | pass | 0 errors |
| every ADC reading collected before the next one replaced it | pass | 0 |
| log file created on the SD card | pass | CONFIG.TXT, RUN00001.BIN |
| file stored in one unbroken piece, card file table consistent | pass | 7680 clusters |
| unused reserved space returned to the card when the file closes | pass | file 3932160 B, 7680 clusters |
| file header records sample rate, gains and run settings | pass | 3906.250 sps, gains [128, 128, 32, 32, 128, 128, 64, 32] |
| no missing or out-of-order data blocks | pass | 7672 blocks |
| no samples lost | pass | 61231 samples, 0 missing |
| samples evenly spaced every 256 µs (within ±5 µs) | pass | 254..258 us |
| sample count in the file trailer matches the data | pass | 61231 |
| each timestamp within 5 µs of the moment the ADC sampled | pass | -0.3..1.7 us |
| all 8 strain-gauge channels match the input to within simulator noise | pass | residual rms 0.35-0.35 uV input-referred, max 1.64 uV (sim noise 0.35 uV) |
| built-in calibration reads the expected -0.874 mV/V (±1 %) on every channel | pass | start -0.8724..-0.8724 mV/V |
| end-of-run calibration within 0.5 % of start (no gain drift) | pass | max 11 ppm |
| shock force from the load cell (CH8) matches the Adams model | pass | rms error 0.57 N, peak 4930 N (truth 4929 N) |
| shock travel sensor (AUX1) matches within 1.5 ADC counts | pass | max 0.61 LSB |
| IMU vertical acceleration matches within 1 mg | pass | max 0.24 mg |
| fresh IMU reading on ≥ 95 % of frames where one is possible (51 % of frames at this rate) | pass | 51.2 % fresh |
| run follows the set order: 1 s settle, 0.5 s calibration at start and end, sensors powered off at the end | pass | settle 1.000 s, shunt on 0.500, 0.500 s |
| CAN bus: start/stop sent, 10 Hz time sync logged, other board's sync received | pass | 156 sync tx events, rx from node [2] |
| writing to the SD card never made the firmware miss an ADC reading | pass | 0 frames read right after an SD step, ring max 59/256 blocks, 4 card stalls |
