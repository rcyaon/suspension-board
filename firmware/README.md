# Firmware

```sh
make -C firmware test        # run the firmware in the PC simulator on the Adams drive (no hardware needed)
make -C firmware check-arm   # compile the STM32 code for Cortex-M4 with clang
make -C firmware             # build firmware.bin (needs arm-none-eabi-gcc); make flash writes it over SWD
python3 firmware/tools/sdlog.py RUN00001.BIN out.csv   # decode a log
```

`src/` is plain C with no vendor libraries. Hardware access sits behind `hal.h`, so the same code runs on the board (`src/stm32`) and in the simulator (`test/sim`). The test drives a rear corner over bumps, whoops, a 0.5 m drop and braking. The input comes from the Adams mini baja spring, damper, tire, hardpoint and mass data. The test checks every logged sample against that input: [results](test/results/summary.md).

## How the firmware works with the board

- **One clock.** The MCU passes its 8 MHz crystal to the ADC through MCO1. Samples and timestamps can't drift apart.
- **The ADC sets the pace.** Each ADC "data ready" (1953 per second by default) triggers an interrupt. It reads all 8 bridges at once, then reads the IMU on its own SPI bus and the latest aux/battery scan, and stores one 60-byte record.
- **Shared SPI bus.** The ADC and SD card share SPI1. The main loop moves 512-byte blocks from a 128 KB RAM buffer to the card in short steps that fit between ADC samples. A slow card stall is absorbed by the buffer. If the card stalls longer than about 1 s, the lost samples are counted and marked in the file.
- **Each run calibrates itself.** At start: excitation on, 1 s settle, then a shunt-cal step (all 8 shunt resistors on together). At stop: the same shunt step, then excitation off. The firmware compares the two steps and flags a gain drift above 0.5%.
- **Raw data only.** ADC codes are stored unchanged. Filter correction and force calibration happen later on a PC.
- **Log file.** Each run is a new `RUNnnnnn.BIN`, preallocated in one piece on a FAT32 card. Its text header holds the rate, gains and the contents of `CONFIG.TXT` from the card: driver, track, tyre pressures, sensor IDs and similar run notes.
- **Control.** Start and stop come from the UART console, a CAN command, or autostart. One board, the CAN master, broadcasts start/stop and a 10 Hz sync so several corner boards can be lined up afterwards. With USB power only, everything except CAN and the 5 V aux sensor supply still works.

**Not yet done:** the firmware has not run on real hardware. USB data download is not implemented, so take the card out to get the logs. The bench tests in `spec.md` (noise, 30-min logging on a real card, fault tests) are still open, along with the sealed connectors and enclosure.
