#!/usr/bin/env python3
"""Check sasm against known code: re-encode every 48-bit instruction of the
stock SHARC program from its ghidra-sharc disassembly and compare the bytes.

usage: sasm_selftest.py [extracted/sharc]     (needs listing.txt, mem48.json,
isa_ranges.txt from the steps in tools/sharc/README.md; nothing is committed)
"""
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sasm

d = sys.argv[1] if len(sys.argv) > 1 else "extracted/sharc"
if not os.path.exists(d + "/listing.txt"):
    print(f"skip: no {d}/listing.txt (derived from the stock firmware, not committed)")
    sys.exit(0)
mem = {int(k, 16): int.from_bytes(bytes.fromhex(v), "little") for k, v in json.load(open(d + "/mem48.json")).items()}
isa = [tuple(int(x, 16) for x in ln.split()[:2]) for ln in open(d + "/isa_ranges.txt")]


def unalias(sw):
    """Short-word alias of a block 0 48-bit address -> that address."""
    if 0x124000 <= sw < isa[-1][1] and (sw + 0x80000) % 3 == 0:
        return (sw + 0x80000) // 3
    return sw


n = ok = alt = 0
bad = collections.Counter()
alts = collections.Counter()
shown = collections.Counter()
for ln in open(d + "/listing.txt"):
    p = ln.rstrip("\n").split("\t")
    if len(p) != 3 or not p[0].startswith("sw:") or p[1] != "6":
        continue
    sw = int(p[0][3:], 16)
    pc = unalias(sw)
    if pc == sw or pc not in mem:
        continue
    if p[2] == "NOP" and mem[pc]:          # a data word that happens to decode
        continue
    text = re.sub(r"0x00([0-9a-f]{6})\b", lambda m: hex(unalias(int(m.group(1), 16))), p[2])
    n += 1
    try:
        enc = sasm.encode(text, sasm.Ctx(pc))
    except sasm.AsmError as e:
        enc, err = [], str(e)
    if enc and enc[0] == mem[pc]:
        ok += 1
    elif mem[pc] in enc:
        alt += 1
        alts[re.sub(r"(?<=[RFIMLBS])\d+", "n", re.sub(r"-?0x[0-9a-f]+", "#", p[2]))] += 1
    else:
        key = re.sub(r"(?<=[RFIMLBS])\d+", "n", re.sub(r"-?0x[0-9a-f]+", "#", p[2]))
        bad[key] += 1
        if shown[key] < 1 and len(shown) < 40:
            shown[key] += 1
            print(f"{pc:06x} {mem[pc]:012x}  {p[2]}\n        " + (" ".join(f"{e:012x}" for e in enc) if enc else err))
if os.environ.get("SASM_ALTS"):
    for k, v in alts.most_common(25):
        print(f"  alt {v:5d}  {k}")
print(f"{n} instructions: {ok} exact, {alt} matched by an alternative encoding, {n - ok - alt} wrong ({len(bad)} shapes)")
sys.exit(1 if n - ok - alt else 0)
