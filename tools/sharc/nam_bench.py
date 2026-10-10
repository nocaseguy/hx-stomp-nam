#!/usr/bin/env python3
"""Build the on-chip NAM benchmark image and check it in the simulator.

usage: nam_bench.py <model.nam | weights.h> [-c image.h NAME]

The program is src/sharc/nam_a2_simd.asm + nam_bench.asm with the model and a
test signal. It is run in ssim twice: once to get the output the SHARC should
produce, and once with that output loaded as the expected values, reading the
report back from the FLAGS line the way src/smoke_nam.c does. The image for
the LPC4350 to download is written with -c.
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

SRCS = [os.path.join(root, "src/sharc", f) for f in ("nam_a2_simd.asm", "report.asm", "nam_bench.asm")]
KERNEL = os.path.join(root, "src/sharc/kernel.asm")
MAGIC = 0x4E414D32
LINE = 0x40                     # FLAG3, the report line of report.asm
WORDS = 18                      # in a frame of nam_bench.asm


def decode(log, words=WORDS):
    """Frames from a FLAGS log [(time, value)]: the line code of report.asm."""
    edges = [(t, bool(v & LINE)) for t, v in log]
    pulses = []                                         # (high time, low time after it)
    for (t0, a), (t1, b), (t2, _) in zip(edges, edges[1:], edges[2:]):
        if a and not b:
            pulses.append((t1 - t0, t2 - t1))
    frames, bits = [], None
    for h, l in pulses:
        if h > 5 * l:
            bits = []
        elif bits is not None:
            bits.append(h > 2 * l)
            if len(bits) == 32 * words:
                frames.append([sum(b << (31 - i) for i, b in enumerate(bits[k:k + 32])) for k in range(0, 32 * words, 32)])
                bits = None
    return frames


def simulate(sections, syms, data, frames=2, pulses=7, limit=60_000_000):
    """Run a program that reports through report.asm until it has sent `frames`
    frames after `pulses` progress pulses."""
    sim = ssim.Sim()
    sim.load(sections)
    for a, words in data:
        for i, v in enumerate(words):
            sim.wr(a + i, v)
    sim.wr(syms["REP_UNIT"], 200)                       # short line-code units: the content is the same
    sim.pc = syms["START"]
    while len(sim.flag_log) < 2 * pulses + frames * (2 + 64 * syms["REP_WORDS"]) + 2:
        sim.step()
        if sim.cycles > limit:
            raise SystemExit("the program does not get to its report")
    return sim


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
    sections, syms = sasm.assemble(mkboot.read_all(SRCS), "nam_bench")
    assert syms["REP_WORDS"] == WORDS
    n = syms["BN"]
    records, rings = nam_pack.place(w, c, syms["MODEL"], syms["X"])
    sig = [0.3 * math.sin(0.31 * i) + (0.5 if i == 3 else 0) for i in range(n)]
    sig = [ssim.f2b(x) for x in sig]

    spans = sorted([(x, x + len(v)) for x, v in records] + [(x, x + ln) for x, ln in rings]
                   + [(s.addr, s.addr + len(s.words)) for s in sections if s.kind == "dm"])
    if any(p[1] > q[0] for p, q in zip(spans, spans[1:])):
        sys.exit("the model and the program's data overlap")

    sim = simulate(sections, syms, records + [(syms["B_IN"], sig)])
    ref = [sim.rd(syms["B_OUT"] + i) for i in range(n)]
    data = records + [(syms["B_IN"], sig + ref)]        # B_REF follows B_IN
    assert syms["B_REF"] == syms["B_IN"] + n
    sim = simulate(sections, syms, data)
    got = decode(sim.flag_log)
    cycles = sim.rd(syms["REP"] + 1)
    want = [MAGIC, cycles, 0, 0xFFFFFFFF]
    peak = max(abs(ssim.b2f(v)) for v in ref)
    ok = (len(got) >= 2 and all(f[:4] == want for f in got)
          and peak > 1e-3 and 1000 * n < cycles < 100000 * n)
    print("words 4-17, as the simulator has them:", " ".join(f"{v:08x}" for v in got[-1][4:]) if got else "-")
    print(f"{c} channels, {n} samples, output peak {peak:.4f}: {cycles} cycles = {cycles / n:.0f} a sample "
          f"({cycles / n / 93.75:.0f}% of 48 kHz at 450 MHz), {len(got)} frames read back  {'ok' if ok else 'MISMATCH'}")
    if not ok:
        sys.exit(1)
    if hdr:
        image = mkboot.build(KERNEL, SRCS, "START", data)
        mkboot.write_header(hdr[0], hdr[1], image, "nam_bench.py and " + os.path.basename(a[0]),
                            f"#define NAM_BENCH_SAMPLES {n}\n#define NAM_BENCH_MAGIC 0x{MAGIC:08x}u\n"
                            f"#define NAM_BENCH_WORDS {WORDS}\n")
        print(f"{hdr[0]}: {len(image)} words ({len(image) * 4} bytes)")


if __name__ == "__main__":
    main()
