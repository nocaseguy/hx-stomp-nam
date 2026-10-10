#!/usr/bin/env python3
"""Build the live NAM program for the SHARC and check its sample routine.

usage: nam_live.py <model.nam | weights.h> [-c image.h NAME]

The program is src/sharc/nam_a2_simd.asm + report.asm + audio.asm +
nam_live.asm with the model. ssim has no serial ports, so what is checked is
live_tick: frames are put in the receive rings, live_tick is called for each,
and what it leaves in the transmit rings is compared with nam_process run on
the same samples in a second simulator. The image for the LPC4350 to download
is written with -c.
"""
import math
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
sys.path.insert(0, here)
import mkboot
import nam_pack
import sasm
import ssim

SRCS = [os.path.join(root, "src/sharc", f) for f in ("nam_a2_simd.asm", "report.asm", "audio.asm", "nam_live.asm")]
KERNEL = os.path.join(root, "src/sharc/kernel.asm")
MODE1 = 0x01010000                                  # RND32, circular buffers
LEVEL = 0.25                                        # nam_live.asm's output gain


def machine(sections, records):
    sim = ssim.Sim()
    sim.load(sections)
    for a, words in records:
        for i, v in enumerate(words):
            sim.wr(a + i, v)
    sim.u[ssim.U_MODE1] = MODE1
    return sim


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def main():
    a = sys.argv[1:]
    hdr = None
    if "-c" in a:
        i = a.index("-c")
        hdr = a[i + 1:i + 3]
        del a[i:i + 3]
    if len(a) != 1:
        sys.exit(__doc__)
    w, c = nam_pack.load(a[0], 8)
    sections, syms = sasm.assemble(mkboot.read_all(SRCS), "nam_live")
    records, rings = nam_pack.place(w, c, syms["MODEL"], syms["X"])
    spans = sorted([(x, x + len(v)) for x, v in records] + [(x, x + ln) for x, ln in rings]
                   + [(s.addr, s.addr + len(s.words)) for s in sections if s.kind == "dm"])
    if any(p[1] > q[0] for p, q in zip(spans, spans[1:])):
        sys.exit("the model and the program's data overlap")

    ln, n, skew = syms["LN"], 40, 10
    live, ref = machine(sections, records), machine(sections, records)
    for sim in (live, ref):
        sim.call(syms["NAM_RESET"])
        sim.wr(syms["NAM_IN"], syms["L_IN"])
        sim.wr(syms["NAM_OUT"], syms["L_OUT"])
        sim.wr(syms["NAM_N"], 1)
    live.wr(syms["L_SKEW"], skew)
    worst, cycles = 0, 0
    for k in range(n):
        rp = 2 * k % ln
        ins = [int(0.25 * math.sin(0.21 * k) * 2 ** 31), int(0.05 * math.sin(0.5 * k + 1) * 2 ** 31), 0,
               int((0.4 if k == 5 else 0) * 2 ** 31), 12345 * k, -7]     # A.L A.R B.L B.R C.L C.R
        for j, v in enumerate(ins):
            live.wr(syms["L_RX"] + (j // 2) * ln + rp + j % 2, v)
        live.wr(syms["L_RP"], rp)
        cycles = max(cycles, live.call(syms["LIVE_TICK"]))
        x = 0.0
        for v in ins:
            x = f32(x + f32(v * 2.0 ** -31))
        ref.wr(syms["L_IN"], ssim.f2b(x))
        ref.call(syms["NAM_PROCESS"])
        y = max(-0.999, min(0.999, f32(ssim.b2f(ref.rd(syms["L_OUT"])) * LEVEL)))
        want = int(round(f32(y) * 2.0 ** 31)) & 0xFFFFFFFF
        wp = (rp + skew) % ln
        got = [live.rd(syms["L_TX"] + p * ln + wp + lr) for p in range(3) for lr in range(2)]
        worst = max(worst, max(abs(ssim.s32(g) - ssim.s32(want)) for g in got))
        if live.rd(syms["L_RP"]) != (rp + 2) % ln:
            sys.exit("live_tick does not advance L_RP")
    ok = worst <= 256                               # the clamp constants are float32, 0.999 is not exact
    print(f"{c} channels, {n} frames through live_tick: largest difference {worst} of 2^31, "
          f"{cycles} instructions a frame ({cycles / 93.75:.0f}% of 48 kHz at 450 MHz)  {'ok' if ok else 'MISMATCH'}")
    if not ok:
        sys.exit(1)
    if hdr:
        image = mkboot.build(KERNEL, SRCS, "START", records)
        mkboot.write_header(hdr[0], hdr[1], image, "nam_live.py and " + os.path.basename(a[0]),
                            f"#define {hdr[1].upper()}_MAGIC 0x{syms and 0x4C495645:08x}u\n")
        print(f"{hdr[0]}: {len(image)} words ({len(image) * 4} bytes)")


if __name__ == "__main__":
    main()
