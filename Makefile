# Build the from-scratch HX Stomp bring-up + DSP firmware.
CROSS  = arm-none-eabi-
ARCH   = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS = $(ARCH) -Os -ffreestanding -Wall -Wextra -Isrc
LDFLAGS= $(ARCH) -T src/lpc4350_ram.ld -nostartfiles \
         --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections
SRCS = src/startup.c src/main.c src/init.c src/delay.c src/uart.c src/i2c.c \
       src/display.c src/post.c src/adc.c src/flash.c src/dfu.c \
       src/dsp/biquad.c src/dsp/engine.c src/dsp/nam_a2.c \
       src/input/encoder.c src/input/switch.c src/input/input.c src/input/input_hw.c
OBJ  = $(patsubst src/%.c,build/%.o,$(SRCS))

all: build/firmware.dfu
backup: build/firmware-backup.dfu
smoke: build/firmware-smoke.dfu

build build/dsp build/input:
	mkdir -p build/dsp build/input
build/%.o: src/%.c | build build/dsp build/input
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
build/firmware.elf: $(OBJ)
	$(CROSS)gcc $(LDFLAGS) $(OBJ) -lm -o $@
	$(CROSS)size $@
build/firmware.bin: build/firmware.elf
	$(CROSS)objcopy -O binary $< $@
build/firmware.dfu: build/firmware.bin
	python3 src/mkbootimg.py $< $@

test:
	$(MAKE) -C test test
build/firmware-backup.elf: $(OBJ)
	$(CROSS)gcc $(LDFLAGS) $(OBJ) -lm -o $@
build/firmware-backup.dfu: CFLAGS += -DHX_BACKUP_MODE
build/firmware-backup.dfu:
	$(MAKE) clean >/dev/null; $(MAKE) CFLAGS="$(CFLAGS) -DHX_BACKUP_MODE" build/firmware.elf
	$(CROSS)objcopy -O binary build/firmware.elf build/firmware-backup.bin
	python3 src/mkbootimg.py build/firmware-backup.bin $@

build/firmware-smoke.dfu:
	$(MAKE) clean >/dev/null; $(MAKE) CFLAGS="$(CFLAGS) -DHX_SMOKE" build/firmware.elf
	$(CROSS)objcopy -O binary build/firmware.elf build/firmware-smoke.bin
	python3 src/mkbootimg.py build/firmware-smoke.bin $@

# Stage-0 proof of life: run, then return to the DFU loader. See src/smoke_bounce.c.
smoke-bounce: build/firmware-smoke-bounce.dfu
build/firmware-smoke-bounce.dfu: src/startup.c src/smoke_bounce.c src/dfu.c | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_bounce.c src/dfu.c -o build/firmware-smoke-bounce.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-bounce.elf build/firmware-smoke-bounce.bin
	python3 src/mkbootimg.py build/firmware-smoke-bounce.bin $@

# Stage-1 proof of life: LCD only (no PLL, no SDRAM). See src/smoke_lcd.c.
smoke-lcd: build/firmware-smoke-lcd.dfu
build/firmware-smoke-lcd.dfu: src/startup.c src/smoke_lcd.c src/dfu.c | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_lcd.c src/dfu.c -o build/firmware-smoke-lcd.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-lcd.elf build/firmware-smoke-lcd.bin
	python3 src/mkbootimg.py build/firmware-smoke-lcd.bin $@

# Our own SHARC program, booted by our own kernel. See tools/sharc/README.md.
SHARC_TOOLS = tools/sharc/mkboot.py tools/sharc/sasm.py
build/sharc_hello.h: src/sharc/kernel.asm src/sharc/hello.asm $(SHARC_TOOLS) | build
	python3 tools/sharc/mkboot.py src/sharc/kernel.asm src/sharc/hello.asm -o build/sharc_hello.bin -c $@ sharc_hello
sharc-test:
	python3 tools/sharc/sasm_selftest.py
	python3 tools/sharc/nam_selftest.py
	python3 tools/sharc/nam_selftest.py test/nam_a2_full.h 120
	python3 tools/sharc/nam_bench.py test/nam_a2_full.h
	python3 tools/sharc/mkreport.py src/sharc/audio.asm src/sharc/audio_probe.asm --set A_WIN=3000 --set A_REPS=2
	python3 tools/sharc/mkreport.py src/sharc/audio.asm src/sharc/audio_thru.asm
	python3 tools/sharc/nam_live.py test/nam_a2_full.h

# Stage-2 proof of life: boot the SHARC with our program. See src/smoke_sharc.c.
smoke-sharc: build/firmware-smoke-sharc.dfu
build/firmware-smoke-sharc.dfu: src/startup.c src/smoke_sharc.c src/smoke_lcd.c build/sharc_hello.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_sharc.c -o build/firmware-smoke-sharc.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-sharc.elf build/firmware-smoke-sharc.bin
	python3 src/mkbootimg.py build/firmware-smoke-sharc.bin $@

# Stage 3: the NAM kernel timed on the SHARC itself. See src/smoke_nam.c.
# Needs an 8-channel model: tools/nam2c.py --channels 8, or a .nam file.
NAM_MODEL ?= test/nam_a2_full.h
NAM_BENCH = src/sharc/kernel.asm src/sharc/nam_a2_simd.asm src/sharc/report.asm src/sharc/nam_bench.asm \
            tools/sharc/nam_bench.py tools/sharc/nam_pack.py tools/sharc/ssim.py
build/sharc_nam_bench.h: $(NAM_MODEL) $(NAM_BENCH) $(SHARC_TOOLS) | build
	python3 tools/sharc/nam_bench.py $(NAM_MODEL) -c $@ sharc_nam_bench
smoke-nam: build/firmware-smoke-nam.dfu
build/firmware-smoke-nam.dfu: src/startup.c src/smoke_nam.c src/smoke_sharc.c src/smoke_lcd.c build/sharc_nam_bench.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_nam.c src/dfu.c -o build/firmware-smoke-nam.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-nam.elf build/firmware-smoke-nam.bin
	python3 src/mkbootimg.py build/firmware-smoke-nam.bin $@

# What the SHARC's pipeline costs: see src/sharc/stall_bench.asm.
build/sharc_stall.h: src/sharc/stall_bench.asm src/sharc/report.asm src/sharc/kernel.asm tools/sharc/mkreport.py $(SHARC_TOOLS) | build
	python3 tools/sharc/mkreport.py src/sharc/stall_bench.asm -c $@ sharc_stall
smoke-stall: build/firmware-smoke-stall.dfu
build/firmware-smoke-stall.dfu: src/smoke_words.c src/startup.c src/smoke_sharc.c src/smoke_lcd.c src/usb_mon.h build/sharc_stall.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) -DIMG=sharc_stall -DIMG_WORDS=SHARC_STALL_WORDS '-DIMG_H="../build/sharc_stall.h"' \
	    src/startup.c src/smoke_words.c src/dfu.c -o build/firmware-smoke-stall.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-stall.elf build/firmware-smoke-stall.bin
	python3 src/mkbootimg.py build/firmware-smoke-stall.bin $@

# Stage 4: the audio hardware. See src/smoke_audio.c (measure) and src/smoke_thru.c (listen).
REPORT = src/sharc/kernel.asm src/sharc/report.asm src/sharc/audio.asm tools/sharc/mkreport.py \
         tools/sharc/nam_bench.py tools/sharc/ssim.py
build/sharc_audio_probe.h: src/sharc/audio_probe.asm $(REPORT) $(SHARC_TOOLS) | build
	python3 tools/sharc/mkreport.py src/sharc/audio.asm src/sharc/audio_probe.asm --set A_WIN=3000 --set A_REPS=2 -c $@ sharc_audio_probe
build/sharc_audio_thru.h: src/sharc/audio_thru.asm $(REPORT) $(SHARC_TOOLS) | build
	python3 tools/sharc/mkreport.py src/sharc/audio.asm src/sharc/audio_thru.asm -c $@ sharc_audio_thru
SMOKE_AUDIO = src/startup.c src/smoke_sharc.c src/smoke_lcd.c src/lcd_panel.h src/smoke_text.h src/smoke_frames.h \
              src/audio_pins.h src/dfu.c
smoke-audio: build/firmware-smoke-audio.dfu
build/firmware-smoke-audio.dfu: src/smoke_audio.c $(SMOKE_AUDIO) build/sharc_audio_probe.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_audio.c src/dfu.c -o build/firmware-smoke-audio.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-audio.elf build/firmware-smoke-audio.bin
	python3 src/mkbootimg.py build/firmware-smoke-audio.bin $@
smoke-thru: build/firmware-smoke-thru.dfu
build/firmware-smoke-thru.dfu: src/smoke_thru.c $(SMOKE_AUDIO) build/sharc_audio_thru.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_thru.c src/dfu.c -o build/firmware-smoke-thru.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-thru.elf build/firmware-smoke-thru.bin
	python3 src/mkbootimg.py build/firmware-smoke-thru.bin $@

# Stage 5: the model in the audio path. See src/smoke_live.c.
NAM_LIVE = src/sharc/kernel.asm src/sharc/nam_a2_simd.asm src/sharc/report.asm src/sharc/audio.asm \
           src/sharc/nam_live.asm tools/sharc/nam_live.py tools/sharc/nam_pack.py tools/sharc/ssim.py
build/sharc_nam_live.h: $(NAM_MODEL) $(NAM_LIVE) $(SHARC_TOOLS) | build
	python3 tools/sharc/nam_live.py $(NAM_MODEL) -c $@ sharc_nam_live
smoke-live: build/firmware-smoke-live.dfu
build/firmware-smoke-live.dfu: src/smoke_live.c $(SMOKE_AUDIO) build/sharc_nam_live.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/smoke_live.c src/dfu.c -o build/firmware-smoke-live.elf
	$(CROSS)objcopy -O binary build/firmware-smoke-live.elf build/firmware-smoke-live.bin
	python3 src/mkbootimg.py build/firmware-smoke-live.bin $@

# The pedal: the model in the path and a monitor for the controls. See src/pedal.c.
pedal: build/firmware-pedal.dfu
build/firmware-pedal.dfu: src/pedal.c src/i2c_poll.h src/usb_mon.h $(SMOKE_AUDIO) build/sharc_nam_live.h | build
	$(CROSS)gcc $(CFLAGS) $(LDFLAGS) src/startup.c src/pedal.c src/dfu.c -lm -o build/firmware-pedal.elf
	$(CROSS)objcopy -O binary build/firmware-pedal.elf build/firmware-pedal.bin
	python3 src/mkbootimg.py build/firmware-pedal.bin $@

clean:
	rm -rf build
.PHONY: all backup smoke smoke-bounce smoke-lcd smoke-sharc smoke-nam smoke-stall smoke-audio smoke-thru smoke-live pedal sharc-test clean test
