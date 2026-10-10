#!/usr/bin/env python3
"""Check src/sharc/nam_a2.asm against src/dsp/nam_a2.c.

usage: nam_selftest.py [model.nam | weights.h] [samples]

Assembles the kernel, runs it in ssim on a test signal and compares every
output sample with the C reference built from the same weights. Also reports
instructions per sample, which is the cycle count less pipeline effects.
Default model: test/nam_a2_model.h (written by tools/nam2c.py, not committed).
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
sys.path.insert(0, here)
import nam_pack
import sasm
import ssim

model = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "test/nam_a2_model.h")
n = int(sys.argv[2]) if len(sys.argv) > 2 else 400
if not os.path.exists(model):
    print(f"skip: no {model}")
    sys.exit(0)
w, c = nam_pack.load(model)

# a decaying two-tone burst with a click: exercises every ring from silence
sig = [0.3 * math.sin(0.031 * i) * math.exp(-i / 300) + 0.1 * math.sin(0.41 * i) + (0.5 if i == 7 else 0) for i in range(n)]
sig = list(struct.unpack(f"<{n}f", struct.pack(f"<{n}f", *sig)))

# ---- reference: the C block, from reset
with tempfile.TemporaryDirectory() as tmp:
    with open(tmp + "/w.h", "w") as f:
        f.write(f"static const float W[{len(w)}] = {{" + ",".join(f"{x!r}f" for x in w) + "};\n")
    with open(tmp + "/main.c", "w") as f:
        f.write('#include "nam_a2.h"\n#include "w.h"\n#include <stdio.h>\n#include <stdlib.h>\n'
                "int main(void){ nam_a2 m; float *st = malloc(nam_a2_state_floats(%d) * sizeof(float));\n"
                "  if (nam_a2_init(&m, %d, W, %d, st)) return 1; float x;\n"
                "  while (fread(&x, 4, 1, stdin) == 1) { float y = nam_a2_tick(&m, x); fwrite(&y, 4, 1, stdout); }\n"
                "  return 0; }\n" % (c, c, len(w)))
    subprocess.run(["cc", "-O1", "-ffp-contract=off", "-I", root + "/src/dsp", "-I", tmp, tmp + "/main.c",
                    root + "/src/dsp/nam_a2.c", "-o", tmp + "/ref", "-lm"], check=True)
    ref = subprocess.run([tmp + "/ref"], input=struct.pack(f"<{n}f", *sig), capture_output=True, check=True).stdout
ref = struct.unpack(f"<{n}f", ref)

# ---- our kernels in the simulator
def run(name, simd):
    src = os.path.join(root, "src/sharc", name)
    sections, syms = sasm.assemble(open(src).read(), src)
    sim = ssim.Sim()
    sim.load(sections)
    if simd:
        records, rings = nam_pack.place(w, c, syms["MODEL"], syms["X"])
        for a, ln in rings:                 # nam_reset has to clear these
            for i in range(ln):
                sim.wr(a + i, 0xDEADBEEF)
    else:
        records, rings = [(syms["MODEL"], nam_pack.pack(w, c, syms["MODEL"]))], []
    spans = [(a, a + len(v)) for a, v in records] + [(a, a + ln) for a, ln in rings]
    spans += [(s.addr, s.addr + len(s.words)) for s in sections if s.kind == "dm"]
    spans.sort()
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        sys.exit(f"{name}: the model and the kernel's data overlap")
    for a, v in records:
        for i, x in enumerate(v):
            sim.wr(a + i, x)
    words = sum(len(v) for a, v in records) + sum(ln for a, ln in rings)
    if simd:
        sim.call(syms["NAM_RESET"])
        sim.cycles = 0
    IN, OUT = 0x400000, 0x410000
    for i, x in enumerate(sig):
        sim.wr(IN + i, ssim.f2b(x))
    sim.wr(syms["NAM_IN"], IN)
    sim.wr(syms["NAM_OUT"], OUT)
    sim.wr(syms["NAM_N"], n)
    cycles = sim.call(syms["NAM_PROCESS"])
    out = [ssim.b2f(sim.rd(OUT + i)) for i in range(n)]
    err = max(abs(a - b) for a, b in zip(out, ref))
    per = cycles / n
    ok = err < 2e-5 and peak > 1e-3 and not sim.simd
    print(f"{name}: max |sharc - C| = {err:.3g}; {per:.0f} instructions per sample = "
          f"{per * 48000 / 450e6 * 100:.0f}% of a 450 MHz core at 48 kHz; model {words} words, "
          f"code {len(sections[-1].words)} instructions  {'ok' if ok else 'MISMATCH'}")
    return ok


peak = max(abs(b) for b in ref)
print(f"{c} channels, {n} samples, reference peak {peak:.4f}")
ok = run("nam_a2.asm", False)
if c % 2 == 0:
    ok = run("nam_a2_simd.asm", True) and ok
print("NAM A2 SHARC KERNELS MATCH" if ok else "MISMATCH")
sys.exit(0 if ok else 1)
