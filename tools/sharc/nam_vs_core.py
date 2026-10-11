#!/usr/bin/env python3
"""Compare the SHARC kernel with NeuralAmpModelerCore on the same samples.

usage: nam_vs_core.py <model.nam> <signal.wav> name=<render> [name=<render> ..]
                      [--slim 1.0] [--channels 8] [--jobs 8] [--keep dir]

signal.wav: mono, 48 kHz, 32-bit float (so both sides start from the same
bits), with at least one receptive field of anything before the part that
counts; --skip says how many samples that is (default 6400, more than the
6347 an A2 model looks back).
<render>: the `render` tool built from NeuralAmpModelerCore (its
tools/render.cpp); several builds can be given, each with a name.

The kernel runs in ssim: src/sharc/nam_a2_simd.asm for 8 channels (the one the
pedal runs, which the unit has been shown to equal bit for bit, src/smoke_nam.c)
and src/sharc/nam_a2.asm for any other number (--channels 3 --slim 0.25 for
A2-Lite: the container takes the first submodel whose limit is above --slim, so 0.25 and not 0.5; that kernel has not run on the unit). ssim is slow, so the signal is
cut into --jobs pieces run side by side; each piece is given the --skip
samples before it first, which is all a WaveNet's output depends on.

Also run: src/dsp/nam_a2.c, built with and without fused multiply-adds.
Printed for every pair: the largest difference, the RMS difference, the
difference against the signal in dB, and NAM's own measure, the error to
signal ratio (ESR).
"""
import json
import math
import multiprocessing
import os
import struct
import subprocess
import sys
import tempfile
import wave

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
sys.path.insert(0, here)
import nam_pack
import sasm
import ssim

SIMD = os.path.join(root, "src/sharc/nam_a2_simd.asm")       # 8 channels: the kernel the pedal runs
PLAIN = os.path.join(root, "src/sharc/nam_a2.asm")           # any number of channels, one at a time


def read_f32(path):
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        sys.exit(f"{path}: not a WAV file")
    p, fmt, data = 12, None, None
    while p + 8 <= len(b):
        tag, n = b[p:p + 4], struct.unpack("<I", b[p + 4:p + 8])[0]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", b[p + 8:p + 24])
        if tag == b"data":
            data = b[p + 8:p + 8 + n]
        p += 8 + n + (n & 1)
    if not fmt or fmt[0] != 3 or fmt[1] != 1 or fmt[5] != 32:
        sys.exit(f"{path}: want mono 32-bit float, got format {fmt}")
    return list(struct.unpack(f"<{len(data) // 4}f", data[:len(data) // 4 * 4]))


def sharc_piece(job):
    w, c, sig = job
    src = SIMD if c == 8 else PLAIN
    sections, syms = sasm.assemble(open(src).read(), src)
    sim = ssim.Sim()
    sim.load(sections)
    if c == 8:
        records, rings = nam_pack.place(w, c, syms["MODEL"], syms["X"])
    else:
        records = [(syms["MODEL"], nam_pack.pack(w, c, syms["MODEL"]))]
    for a, v in records:
        for i, x in enumerate(v):
            sim.wr(a + i, x)
    if c == 8:
        sim.call(syms["NAM_RESET"])
    IN, OUT = 0x400000, 0x480000
    for i, x in enumerate(sig):
        sim.wr(IN + i, ssim.f2b(x))
    sim.wr(syms["NAM_IN"], IN)
    sim.wr(syms["NAM_OUT"], OUT)
    sim.wr(syms["NAM_N"], len(sig))
    sim.call(syms["NAM_PROCESS"], limit=30_000 * len(sig))
    return [ssim.b2f(sim.rd(OUT + i)) for i in range(len(sig))]


def c_reference(w, c, sig, contract, tmp):
    with open(tmp + "/w.h", "w") as f:
        f.write(f"static const float W[{len(w)}] = {{" + ",".join(f"{x!r}f" for x in w) + "};\n")
    with open(tmp + "/main.c", "w") as f:
        f.write('#include "nam_a2.h"\n#include "w.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
                "int main(void){ nam_a2 m; float *st = malloc(nam_a2_state_floats(%d) * sizeof(float));\n"
                "  if (nam_a2_init(&m, %d, W, %d, st)) return 1; float x;\n"
                "  while (fread(&x, 4, 1, stdin) == 1) { float y = nam_a2_tick(&m, x); fwrite(&y, 4, 1, stdout); }\n"
                "  return 0; }\n" % (c, c, len(w)))
    exe = f"{tmp}/ref_{contract}"
    subprocess.run(["cc", "-O2", f"-ffp-contract={contract}", "-I", root + "/src/dsp", "-I", tmp, tmp + "/main.c",
                    root + "/src/dsp/nam_a2.c", "-o", exe, "-lm"], check=True)
    out = subprocess.run([exe], input=struct.pack(f"<{len(sig)}f", *sig), capture_output=True, check=True).stdout
    return list(struct.unpack(f"<{len(sig)}f", out))


def compare(name, a, b):
    """a against b, b being the one called right."""
    n = len(a)
    d = [x - y for x, y in zip(a, b)]
    big = max(abs(x) for x in d)
    e2, s2 = sum(x * x for x in d), sum(y * y for y in b)
    same = sum(struct.pack("<f", x) == struct.pack("<f", y) for x, y in zip(a, b))
    if e2 == 0:
        print(f"  {name:44s} equal, all {n} samples")
        return
    rms = math.sqrt(e2 / n)
    print(f"  {name:44s} max {big:.3g} ({20 * math.log10(big):.1f} dBFS, {big * 2 ** 23:.2f} LSB of 24)  "
          f"rms {rms:.3g} ({20 * math.log10(rms):.1f} dBFS)  signal/error {10 * math.log10(s2 / e2):.1f} dB  "
          f"ESR {e2 / s2:.3g}  same bits {100 * same / n:.1f}%")


def main():
    args = sys.argv[1:]

    def opt(name, default):
        if name in args:
            i = args.index(name)
            v = args[i + 1]
            del args[i:i + 2]
            return v
        return default
    slim, c = opt("--slim", "1.0"), int(opt("--channels", "8"))
    jobs, skip, keep = int(opt("--jobs", "8")), int(opt("--skip", "6400")), opt("--keep", None)
    if len(args) < 2:
        sys.exit(__doc__)
    model, wav, renders = args[0], args[1], [a.split("=", 1) for a in args[2:]]
    w, c = nam_pack.load(model, c)
    sig = read_f32(wav)
    n = len(sig) - skip
    print(f"{os.path.basename(model)}: {c} channels, {len(w)} weights; {n} samples compared after {skip}")

    outs = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, exe in renders:
            subprocess.run([exe, "--slim", slim, model, wav, tmp + "/o.wav"], check=True, capture_output=True)
            outs[name] = read_f32(tmp + "/o.wav")[skip:]
        outs["C reference, no fused multiply-add"] = c_reference(w, c, sig, "off", tmp)[skip:]
        outs["C reference, fused multiply-add"] = c_reference(w, c, sig, "fast", tmp)[skip:]

    size = -(-n // jobs)
    pieces = [(w, c, sig[s:s + skip + size]) for s in range(0, n, size)]
    with multiprocessing.Pool(jobs) as pool:
        sharc = [x for piece in pool.map(sharc_piece, pieces) for x in piece[skip:]]
    outs["SHARC kernel"] = sharc[:n]

    if keep:
        os.makedirs(keep, exist_ok=True)
        for name, o in outs.items():
            with open(os.path.join(keep, name.replace(" ", "_").replace(",", "") + ".f32"), "wb") as f:
                f.write(struct.pack(f"<{len(o)}f", *o))

    first = renders[0][0] if renders else "C reference, no fused multiply-add"
    ref = outs[first]
    peak, rms = max(abs(x) for x in ref), math.sqrt(sum(x * x for x in ref) / n)
    print(f"output of {first}: peak {peak:.4f} ({20 * math.log10(peak):.1f} dBFS), "
          f"rms {rms:.4f} ({20 * math.log10(rms):.1f} dBFS)")
    names = list(outs)
    for i, b in enumerate(names):
        print(f"against {b}:")
        for a in names[i + 1:]:
            compare(a, outs[a], outs[b])


if __name__ == "__main__":
    main()
