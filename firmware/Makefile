# make            firmware image (needs arm-none-eabi-gcc)
# make check-arm  compile the target code for Cortex-M4 with clang (no linker needed)
# make test       run the firmware in the PC simulator against the Adams drive
# make flash      program over SWD (needs st-flash)

CORE  = src/app.c src/ads131m08.c src/icm42688.c src/sd_spi.c src/fat32.c src/util.c
STM32 = src/stm32/hal_stm32.c src/stm32/startup.c
SIM   = test/sim/hal_sim.c test/sim/dev_ads.c test/sim/dev_sd.c test/sim/dev_imu.c test/sim/sim_main.c

CROSS ?= arm-none-eabi-
MCU    = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
WARN   = -Wall -Wextra -Wno-unused-parameter
ARMFLAGS = $(MCU) -Os -g -ffreestanding -ffunction-sections -fdata-sections $(WARN)

all: build/firmware.bin

build/firmware.elf: $(CORE) $(STM32) src/*.h src/stm32/*.h src/stm32/stm32f412ce.ld
	@mkdir -p build
	$(CROSS)gcc $(ARMFLAGS) -nostdlib -T src/stm32/stm32f412ce.ld -Wl,--gc-sections -o $@ $(CORE) $(STM32) -lgcc
	$(CROSS)size $@

build/firmware.bin: build/firmware.elf
	$(CROSS)objcopy -O binary $< $@

check-arm:
	@mkdir -p build/arm-check
	@for f in $(CORE) $(STM32); do \
	  clang --target=thumbv7em-none-eabihf $(MCU) -Os -ffreestanding -fno-builtin $(WARN) -Werror \
	    -c $$f -o build/arm-check/$$(basename $$f .c).o || exit 1; done
	@echo "target sources compile cleanly for Cortex-M4"

build/sim: $(CORE) $(SIM) src/*.h test/sim/*.h
	@mkdir -p build
	cc -O2 -g $(WARN) -o $@ $(CORE) $(SIM) -lm

sim: build/sim

test: build/sim
	python3 test/run_tests.py

flash: build/firmware.bin
	st-flash --reset write $< 0x08000000

clean:
	rm -rf build

.PHONY: all check-arm sim test flash clean
