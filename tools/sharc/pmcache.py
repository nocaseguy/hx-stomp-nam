#!/usr/bin/env python3
"""Where the instructions that read or write data through PM sit in the
instruction cache.

usage: pmcache.py file.asm [first_label last_label]

An instruction with a PM data access needs the program-memory bus in the cycle
the next instruction is fetched through it, so the ADSP-21469 keeps the
fetched instruction in a cache of 32 entries: 16 sets chosen by the low four
bits of the address, two entries a set. A third such instruction with the same
low four bits throws one of the others out, and each one that is not in the
cache when it runs costs a cycle. This lists the sets with more than two
between the two labels (default: all of the file's code).
"""
import re
import sys


def pm_instructions(path):
    """[(address, text)] of every instruction with a PM data access, and the labels."""
    src = re.sub(r"/\*.*?\*/", "", open(path).read(), flags=re.S)
    out, labels, addr, code = [], {}, 0, False
    for line in src.split("\n"):
        line = line.split("//")[0]
        while True:
            m = re.match(r"\s*([A-Za-z_.]\w*):", line)
            if not m:
                break
            labels[m.group(1).upper()] = addr
            line = line[m.end():]
        for s in line.split(";"):
            s = s.strip()
            if not s:
                continue
            m = re.match(r"\.(\w+)\s*(.*)$", s)
            if m:
                if m.group(1).lower() == "section":
                    k, a = m.group(2).split()
                    code, addr = k == "pm", int(a, 0)
                continue
            if code:
                if "PM(" in s.upper():
                    out.append((addr, s))
                addr += 1
    return out, labels


def main():
    a = sys.argv[1:]
    if len(a) not in (1, 3):
        sys.exit(__doc__)
    ins, labels = pm_instructions(a[0])
    lo, hi = (labels[a[1].upper()], labels[a[2].upper()]) if len(a) == 3 else (0, 1 << 32)
    ins = [(x, s) for x, s in ins if lo <= x < hi]
    sets = {}
    for x, s in ins:
        sets.setdefault(x & 15, []).append((x, s))
    over = 0
    print(f"{len(ins)} instructions with a PM data access; per set:",
          " ".join(str(len(sets.get(i, []))) for i in range(16)))
    for i in sorted(sets):
        if len(sets[i]) > 2:
            over += len(sets[i]) - 2
            print(f"set {i:x}:")
            for x, s in sets[i]:
                print(f"   {x:#x}  {s}")
    print(f"{over} more than fit")
    return over


if __name__ == "__main__":
    main()
