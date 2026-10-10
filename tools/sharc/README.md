# SHARC (ADSP-21469)

The tools for writing a SHARC program of our own without any ADI toolchain,
and the programs written with them. `HARDWARE.md` lists what they have to set
up on this board.

## Our own program

| | |
|---|---|
| `sasm.py` | assembler for 48-bit SHARC code; syntax in its docstring |
| `sasm_selftest.py` | re-encodes the disassembly of all 11,633 stock ISA instructions and compares bytes (`make sharc-test`) |
| `mkboot.py` | kernel + program sources (+ data) -> boot image (format in its docstring) |
| `ssim.py` | simulator for the code we write: runs the encoded instructions, stops on anything it does not model |
| `nam_pack.py` | lays a NAM A2 model out for the SHARC kernels, for the SIMD one across the internal memory blocks |
| `nam_selftest.py` | runs `nam_a2.asm` in `ssim` and compares every sample with `src/dsp/nam_a2.c` (`make sharc-test`) |
| `src/sharc/nam_a2.asm` | NAM A2 block, 106 instructions, any channel count |
| `src/sharc/nam_a2_simd.asm` | A2-Full (8 channels) on both processing elements |
| `src/sharc/kernel.asm` | our boot kernel: receives `address, count, words` records over SPI, then jumps |
| `src/sharc/hello.asm` | first program: toggles FLAG2/FLAG3 |
| `src/smoke_sharc.c` | LPC4350 image that resets the SHARC, downloads `hello` and shows the result on the LCD (`make smoke-sharc`) |
| `src/sharc/nam_bench.asm` | runs the SIMD kernel on the SHARC with the core timer running and reports cycles and output error on a flag line |
| `nam_bench.py` | builds that program with a model and a test signal, runs it in `ssim`, reads the report back from the simulated flag line, writes the boot image |
| `src/smoke_nam.c` | LPC4350 image that downloads the benchmark and shows its report on the LCD (`make smoke-nam`) |
| `src/sharc/report.asm` | what the test programs share: frames on the flag line, progress pulses, the PLL set-up |
| `mkreport.py` | builds a test program with `report.asm`, runs it in `ssim`, prints its frames, writes the boot image |
| `src/sharc/audio_probe.asm` | sets up routing, audio clock generators and the receiving serial ports, then reports words received and peak level per port |
| `src/smoke_audio.c` | LPC4350 image that boots the probe and shows its report (`make smoke-audio`) |
| `src/sharc/audio.asm` | the audio hardware from the SHARC: routing, clock generators, codec reset, serial ports |
| `src/sharc/audio_thru.asm`, `src/smoke_thru.c` | inputs copied to outputs through the codecs (`make smoke-thru`): works on the unit |
| `src/sharc/nam_live.asm`, `nam_live.py`, `src/smoke_live.c` | the model in the audio path, ports on chained DMA (`make smoke-live`) |
| `src/pedal.c` | the pedal (`make pedal`): `nam_live.asm`'s chain model -> three-band equaliser -> delay -> level, worked from the front panel; on the unit the program keeps up with every block on |
| `src/audio_pins.h` | the LPC4350's share of the audio path: bypass relays and the stock start-up pin levels |

NAM A2 on the SHARC. In the simulator (`make sharc-test`; the 8-channel runs
need `test/nam_a2_full.h` from `tools/nam2c.py --channels 8`) the output equals
the C reference to 1e-7 in every case. Cost is instructions per sample, as a
share of the 450 MHz core at 48 kHz (9,375 cycles a sample):

| model | kernel | instructions | share | model |
|---|---|---|---|---|
| A2-Lite, 3 channels | `nam_a2.asm` | 5,251 | 56% | 21,150 words |
| A2-Full, 8 channels | `nam_a2.asm` | 19,756 | 211% | 63,275 words |
| A2-Full, 8 channels | `nam_a2_simd.asm` | 7,403 | 79% | 63,500 words |

`nam_a2_simd.asm` uses both processing elements. A2-Full has about 12,100
multiply-adds a sample, 6,070 cycles at two a cycle (65%); the rest is loop
set-up and the head. The model fits internal memory:
`nam_pack.place()` puts the weights and the layer rings, which the kernel
reads through PM, in blocks 3 and 1, and what it reads through DM in block 2,
so no instruction reads one block twice. External memory would be far too slow
for the rings. `nam_a2.asm` for A2-Lite is the unoptimised form.

## On the unit

`make smoke-nam`, hold Page Right while powering up, then
`dfu-util -d 1fc9:000c -a 0 -D build/firmware-smoke-nam.dfu`. The LPC4350
boots the SHARC with `nam_bench.asm`, which runs A2-Full on the chip, times it
with the core timer and reports on a flag line; the screen shows the report as
hex (layout at the top of `src/smoke_nam.c`). `make smoke-sharc` is the
smaller test of the boot path alone.

Measured, A2-Full, 32 samples from reset:

| kernel | cycles a sample | share of 48 kHz at 450 MHz | output |
|---|---|---|---|
| 1x1 as straight-line code (7,955 instructions) | 9,523 | 102% | equal to the simulator's, bit for bit |
| 1x1 as loops (8,047 instructions) | 8,954 | 95.5% | equal to the simulator's, bit for bit |
| taps read in place in the ring, the present kernel (7,403 instructions) | 8,144 | 86.9% | equal to the simulator's, bit for bit |

The simulator counts instructions; the chip takes 11% more cycles with the
present kernel. An instruction that reads data through PM needs a place in
the 32-entry instruction cache: the straight-line 1x1 had about 54 such
instructions in a layer's path and the looped one about 30, which is where the
570 cycles went. What the simulator got wrong about the processor, each found
this way, is listed at the top of `HARDWARE.md`.

The benchmark sets the core clock up as the stock program does; that works
(450 MHz, measured as a ratio against the 400 MHz the chip starts at).

**The model plays on the unit** (`make smoke-live`): guitar in, NAM A2-Full
out of every output, driven, with no delay or stutter to the ear. Clocks,
routing, the codecs' reset and the serial ports are in `audio.asm`; the ports
run on chained DMA rings in `nam_live.asm`. `make smoke-thru` is the same path
without the model.

Not there yet: the volume knob and every other control (nothing reads them),
choosing a model at run time (it is built into the image), which jack is on
which port and slot (all inputs are summed to all outputs), and a measured
figure for how much of the sample period the live loop uses.
