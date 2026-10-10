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
- **NAM A2-Full (8 channels) live in the signal path**: equal to the reference
  implementation bit for bit, 8,954 core cycles a sample (95.5% of a 48 kHz
  period)

Not there yet: reading any control (knobs, buttons, footswitches and the
volume knob all sit behind a controller on the footswitch board), choosing a
model at run time, knowing which jack is on which port (all inputs are summed
to all outputs), anything persistent.

## Try it

You need `arm-none-eabi-gcc`, Python 3, `dfu-util`, a C compiler for the
tests, and a NAM A2 model of your own (none is included).

```
python3 tools/nam2c.py your_model.nam test/nam_a2_full.h --channels 8
make sharc-test          # assembler, simulator, kernels against the C reference
make smoke-live          # -> build/firmware-smoke-live.dfu
```

Hold Page Right while powering the unit up; it appears as `1fc9:000c`. Then,
with your amp or headphones turned down (there is no volume control yet):

```
dfu-util -d 1fc9:000c -a 0 -D build/firmware-smoke-live.dfu
```

`RECOVERY.md` has the details of loading and getting back. Smaller steps, in
order, if something does not work: `make smoke-lcd`, `smoke-sharc`,
`smoke-nam`, `smoke-audio`, `smoke-thru`.

## Where things are

| | |
|---|---|
| `src/sharc/` | SHARC sources: boot kernel, NAM kernels, audio set-up, test programs |
| `tools/sharc/` | `sasm` (assembler), `ssim` (simulator), image builders, tests; see its README |
| `src/dsp/nam_a2.c` | the plain C reference the SHARC kernels are checked against |
| `src/smoke_*.c`, `src/pedal.c` | LPC4350 images that boot the SHARC and show results |
| `src/` (the rest) | an LPC4350 firmware skeleton: start-up, clocks, display, inputs |
| `HARDWARE.md`, `tools/sharc/HARDWARE.md` | what the board needs, as found |

## Which unit

The work was done on an HX Stomp. The hardware facts it rests on were read
from the HX Stomp XL firmware and turned out to hold on the HX Stomp, so the
two evidently share the processor module and audio section; the XL itself has
not been tried. The control model in `src/input/` (eight footswitches) is an
early guess from XL firmware strings and matches neither unit as far as is
known.

## Licence

GPL-3.0-or-later; see `LICENSE` and `NOTICE`. The hardware facts in the
`HARDWARE.md` files were worked out for interoperability with a device the
author owns.
