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

GPL-3.0-or-later; see `LICENSE` and `NOTICE`. The hardware facts in the
`HARDWARE.md` files were worked out for interoperability with a device the
author owns.
