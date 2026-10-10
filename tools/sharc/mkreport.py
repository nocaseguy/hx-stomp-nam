#!/usr/bin/env python3
"""Build a SHARC test program that reports through src/sharc/report.asm.

usage: mkreport.py program.asm [more.asm ...] [-c image.h NAME] [--set LABEL=VALUE ...]

Assembles report.asm + the program (several files are one source, in order), runs it in ssim until it has sent two
frames, prints them, and with -c writes the boot image for an LPC4350 smoke
image, with NAME_WORDS and NAME_MAGIC. --set changes a data word for the
simulation only (a long timing constant, say). The simulator has no
peripherals: their registers read as memory, so this checks the program's flow
and its report, not what it measures.
"""
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
sys.path.insert(0, here)
import mkboot
import nam_bench
import sasm

REPORT = os.path.join(root, "src/sharc/report.asm")
KERNEL = os.path.join(root, "src/sharc/kernel.asm")


def main():
    a = sys.argv[1:]
    hdr, sets = None, {}
    if "-c" in a:
        i = a.index("-c")
        hdr = a[i + 1:i + 3]
        del a[i:i + 3]
    while "--set" in a:
        i = a.index("--set")
        k, v = a[i + 1].split("=")
        sets[k.upper()] = int(v, 0)
        del a[i:i + 2]
    if not a:
        sys.exit(__doc__)
    srcs = [REPORT] + a
    try:
        sections, syms = sasm.assemble(mkboot.read_all(srcs), os.path.basename(a[-1]))
    except sasm.AsmError as e:
        sys.exit(str(e))
    words = syms["REP_WORDS"]
    sim = nam_bench.simulate(sections, syms, [(syms[k], [v]) for k, v in sets.items()], pulses=8)   # room for a few progress pulses
    got = nam_bench.decode(sim.flag_log, words)
    for f in got:
        print(" ".join(f"{v:08x}" for v in f))
    if len(got) < 2:
        sys.exit("fewer than two frames read back")
    if hdr:
        image = mkboot.build(KERNEL, srcs, "START")
        up = hdr[1].upper()
        mkboot.write_header(hdr[0], hdr[1], image, "mkreport.py and " + os.path.basename(a[-1]),
                            f"#define {up}_WORDS {words}\n#define {up}_MAGIC 0x{got[0][0]:08x}u\n")
        print(f"{hdr[0]}: {len(image)} words ({len(image) * 4} bytes)")


if __name__ == "__main__":
    main()
