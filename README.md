# hx-stomp-nam

Independent firmware for the HX Stomp that runs a
[Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) A2
model on the unit's own DSP. Everything here is written from scratch: a boot
loader and programs for the ADSP-21469 SHARC, an assembler and simulator for
it (no Analog Devices toolchain is needed), and small images for the LPC4350
that load them.

**Unaffiliated.** This is not a Line 6 product and has nothing of Line 6's in
it. It is loaded into RAM over USB and is gone at the next power-cycle; the
unit's own firmware is not touched. You run it at your own risk.

## What works

On a real HX Stomp (the three-footswitch model; see "Which unit" below):

- loading an image over USB with `dfu-util`, and returning to the loader
- the display
- booting the SHARC with our own loader and program, at 450 MHz
- audio in and out through the three CS4272 codecs, bypass relays switched
- **NAM A2-Full (8 channels) live in the signal path**: 8,144 core cycles a
  sample (86.9% of a 48 kHz period). Its output is the simulator's bit for
  bit, and within 1.3e-7 of full scale of the C reference in `src/dsp/`
  (same arithmetic, sums taken in a different order)
- **the same output as NeuralAmpModelerCore, to 129 dB**: see "How close to
  NAM" below
- the front panel: every knob, button and footswitch, footswitch touch, and
  the footswitch rings (`HARDWARE.md` has the messages)
- **a small pedal** (`make pedal`): amp -> three-band equaliser -> delay ->
  volume, drawn as a chain on the screen, blocks chosen and switched from the
  panel and the footswitches, values on the three knobs. With the amp on the
  SHARC is 89% busy
- **the same pedal in full colour** (`make stomp`): the 32 MB SDRAM, the
  screen at 16 bits a pixel, and a UI with a header, a body and a footer
  that says what each knob does (`src/ui/`). Its pages can be drawn on a
  computer with no unit: `make -C test/sim pedal`
- text out and commands in over the USB socket once an image is running
  (`tools/usbmon.py`), including "back to the loader", so only the first load
  after power-up needs the button held

Not there yet: the volume knob's position before it is first moved, choosing
a model at run time, knowing which jack is on which port (all inputs are
summed to all outputs), stereo, presets, anything persistent. A2-Full leaves
little of the processor for more effects.

## Try it

You need `arm-none-eabi-gcc`, Python 3, `dfu-util`, a C compiler for the
tests, and a NAM A2 model of your own (none is included).

```
python3 tools/nam2c.py your_model.nam test/nam_a2_full.h --channels 8
make sharc-test          # assembler, simulator, kernels against the C reference
make pedal               # -> build/firmware-pedal.dfu : chain on a 16-colour screen
make stomp               # -> build/firmware-stomp.dfu : the same chain, full-colour UI
```

Hold Page Right while powering the unit up; it appears as `1fc9:000c`. Then,
with your amp or headphones turned down (the level starts at a fixed value
until the volume knob is first moved):

```
dfu-util -d 1fc9:000c -a 0 -D build/firmware-pedal.dfu
```

Controls are listed at the top of `src/pedal.c`, and for `stomp` at the top
of `src/ui/pages.h`. `python3 tools/usbmon.py` (needs pyusb) shows what the
controls send; `--send D` returns the unit to the loader for the next image,
and under `stomp` `--shot file.png` fetches the screen. If `stomp` shows
nothing, `make smoke-sdram` tries the SDRAM and the 16-bit screen alone.

An A2 model's output stays some 18 dB under full scale however hard it is
driven, so the amp has a LEVEL that starts at +12 dB.

`RECOVERY.md` has the details of loading and getting back. Smaller steps, in
order, if something does not work: `make smoke-lcd`, `smoke-sharc`,
`smoke-nam`, `smoke-audio`, `smoke-thru`, `smoke-live`.

## How close to NAM

The kernel was run against the `render` tool of
[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore)
(commit 2563c0f, built its own way) on that project's own
`example_models/A2.nam`: one second at 48 kHz, a quarter each of a sine sweep,
white noise, a passage of NAM's capture signal and a passage of a guitar DI.
The difference from NeuralAmpModelerCore's default build:

| | largest | RMS | signal / error | ESR |
|---|---|---|---|---|
| **A2-Full, the SHARC kernel on the unit** | 3.4e-7 (-129.3 dBFS) | 3.6e-8 (-149.0 dBFS) | **129.4 dB** | 1.2e-13 |
| A2-Full, NeuralAmpModelerCore's generic WaveNet | 1.0e-7 | 1.7e-8 | 135.7 dB | 2.7e-14 |
| **A2-Lite, the SHARC kernel (simulator only)** | 4.6e-7 (-126.7 dBFS) | 4.7e-8 (-146.5 dBFS) | **127.0 dB** | 2.0e-13 |
| A2-Lite, NeuralAmpModelerCore's generic WaveNet | 4.8e-7 | 5.4e-8 | 125.8 dB | 2.6e-13 |

The largest difference is under four steps of a 24-bit converter. Nothing
here equals anything else bit for bit, NeuralAmpModelerCore's two builds
included: floating-point sums taken in a different order round differently.
The SHARC kernel is about as far from NeuralAmpModelerCore as that library's
two builds are from each other. ESR is NAM's own measure of error; a good
capture of an amplifier is around 1e-2 to 1e-3 against the amplifier.

The comparison ran in the simulator, whose output the unit has been shown to
equal bit for bit for the 8-channel kernel. `tools/sharc/nam_vs_core.py` does
the measurement and `tools/sharc/README.md` has the rest of the numbers. It
needs a `render` built from NeuralAmpModelerCore and a 32-bit float WAV file
of your own.

## Where things are

| | |
|---|---|
| `src/sharc/` | SHARC sources: boot kernel, NAM kernels, audio set-up, test programs |
| `tools/sharc/` | `sasm` (assembler), `ssim` (simulator), image builders, tests; see its README |
| `src/dsp/nam_a2.c` | the plain C reference the SHARC kernels are checked against |
| `src/pedal.c` | the pedal: chain, screen, controls |
| `src/stomp.c`, `src/ui/` | the pedal with the full-colour UI; `test/sim/pedal_sim.c` draws it on a computer |
| `tools/mkfont.py`, `tools/fonts/` | the UI's two faces (SIL OFL) and what makes `src/ui/font.h` from them |
| `src/smoke_*.c` | LPC4350 images that boot the SHARC with one test each and show the result |
| `src/` (the rest) | an LPC4350 firmware skeleton: start-up, clocks, display, inputs |
| `HARDWARE.md`, `tools/sharc/HARDWARE.md` | what the board needs, as found |

## Which unit

The work was done on an HX Stomp. The hardware facts it rests on were first
read from the HX Stomp XL firmware and then checked against the HX Stomp's
own: the two use the same SHARC program and, for everything used here, the
same code on the LPC4350 (`HARDWARE.md` has the details). The XL itself has
not been tried. The control model in `src/input/` (eight footswitches) is an
early guess from XL firmware strings and matches neither unit as far as is
known.

## Licence

GPL-3.0-or-later; see `LICENSE` and `NOTICE`.

## What is and is not in this repository

- The programs here were written by the author. `NOTICE` lists what was
  not: an opcode table and two fonts, each under its own free licence. There
  is no Line 6 code,
  firmware, firmware update file, preset, picture or sound in it, and none is
  needed to build or run it.
- No NAM model is included; you bring your own.
- Nothing is written to the unit's storage. The images are loaded into RAM
  through the USB loader in the processor's boot ROM, which the chip's maker
  documents, and are gone at the next power-cycle. No protection is removed
  or got round to do that: the loader takes any image.
- The `HARDWARE.md` files are a record of what the board needs: which pin is
  which, register values, the form of the messages between the parts. They
  were worked out, on a unit the author owns, from the unit's own firmware
  and by trying things on the hardware, for one purpose: to make
  independently written programs work with that hardware.
- "Line 6", "Helix" and "HX Stomp" are used only to say which device this
  runs on. This is not a Line 6 product and Line 6 has no part in it.

If you are at Line 6 or Yamaha Guitar Group and want something here changed
or discussed, open an issue; it will get an answer.
