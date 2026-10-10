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


def effects(sections, records, syms):
    """The gate, the three bands and the delay, model off, against the same
    arithmetic done here."""
    par = [1.5, 0, 0.5, 0, 0.02, 0.2, 0.05, 0.9,
           1.02, -1.9, 0.9, 1.9, -0.92, 0.9, -1.2, 0.5, 1.3, -0.45, 1.1, 0.3, 0.1, -0.25, -0.05,
           7, 0.5, 0.75]
    sim = machine(sections, records)
    sim.call(syms["FX_RESET"])
    for i, v in enumerate(par):
        sim.wr(syms["L_PAR"] + i, v if i in (1, 3, 23) else ssim.f2b(v))
    for b in range(3):                              # the bands go to use by message 26
        sim.wr(syms["L_MSG"], 26)
        sim.wr(syms["L_MSG"] + 1, b)
        sim.wr(syms["SPIDMAC"], 0)                  # "the message is in"
        sim.wr(syms["IISP0A"], syms["L_RX"])        # and no frame is waiting
        sim.wr(syms["L_RP"], 0)
        sim.pc = syms["LIVE"]
        while sim.pc != syms["ARM"]:
            sim.step()
    if [sim.rd(syms["EQ_C"] + i) for i in range(15)] != [ssim.f2b(v) for v in par[8:23]]:
        sys.exit("message 26 does not put the bands to use")
    ln, skew, worst = syms["LN"], 10, 0.0
    sim.wr(syms["L_SKEW"], skew)
    env = g = 0.0
    st = [0.0] * 6
    line = [0.0] * 7
    wp = 0
    for k in range(120):
        x = 0.3 * math.sin(0.3 * k) if 10 <= k < 50 or k >= 90 else 0.001 * math.sin(0.3 * k)
        rp = 2 * k % ln
        for j in range(6):
            sim.wr(syms["L_RX"] + (j // 2) * ln + rp + j % 2, int(x * 2 ** 31) if j == 0 else 0)
        sim.wr(syms["L_RP"], rp)
        sim.call(syms["LIVE_TICK"])
        x = f32(int(x * 2 ** 31) * 2.0 ** -31)
        env = max(env * par[7], abs(x))
        g += ((1.0 if env >= par[4] else 0.0) - g) * (par[5] if env >= par[4] else par[6])
        y = x * g * par[0]
        for b in range(3):
            b0, b1, b2, na1, na2 = par[8 + 5 * b:13 + 5 * b]
            o = b0 * y + st[2 * b]
            st[2 * b] = b1 * y + na1 * o + st[2 * b + 1]
            st[2 * b + 1] = b2 * y + na2 * o
            y = o
        d = line[wp]
        line[wp] = y + par[24] * d
        wp = (wp + 1) % par[23]
        y = max(-0.999, min(0.999, (y + par[25] * d) * par[2]))
        got = ssim.s32(sim.rd(syms["L_TX"] + (rp + skew) % ln)) * 2.0 ** -31
        worst = max(worst, abs(got - y))
    ok = worst < 2e-5
    print(f"gate, three bands and delay, 120 frames: largest difference {worst:.1e}  {'ok' if ok else 'MISMATCH'}")
    if not ok:
        sys.exit(1)


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
                   + [(s.addr, s.addr + len(s.words)) for s in sections if s.kind == "dm"]
                   + [(syms["D_BUF"], syms["D_BUF"] + syms["D_MAX"])])
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
    live.wr(syms["G_GAIN"], ssim.f2b(1.0))          # the gate already open
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
    effects(sections, records, syms)
    if hdr:
        image = mkboot.build(KERNEL, SRCS, "START", records)
        mkboot.write_header(hdr[0], hdr[1], image, "nam_live.py and " + os.path.basename(a[0]),
                            f"#define {hdr[1].upper()}_MAGIC 0x{syms and 0x4C495645:08x}u\n")
        print(f"{hdr[0]}: {len(image)} words ({len(image) * 4} bytes)")


if __name__ == "__main__":
    main()
