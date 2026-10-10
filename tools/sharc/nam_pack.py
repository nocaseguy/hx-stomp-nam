#!/usr/bin/env python3
"""Lay a NAM A2 model out for src/sharc/nam_a2.asm.

usage: nam_pack.py <model.nam | weights.h> [--channels 3|8] [--base 0xc0100] [-o blob.bin]

pack(weights, channels, base) returns the 32-bit words of the model block
nam_a2.asm reads at `base`: header, layer table, weights in the order the
kernel reads them, then the zeroed rings. place() does the same for
nam_a2_simd.asm, spread over the processor's memory blocks. The layouts are
described in the two kernels.
"""
import json
import re
import struct
import sys

KERNEL = [6] * 14 + [15, 15] + [6] * 7
DILATION = [1, 3, 7, 17, 41, 101, 239] * 2 + [1, 13] + [1, 3, 7, 17, 41, 101, 239]
HEAD_TAPS = 16


def f2b(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def load(path, channels=3):
    """Weight stream of a .nam file, or of a header written by tools/nam2c.py."""
    text = open(path).read()
    if path.endswith(".h"):
        body = text[text.index("{") + 1: text.rindex("}")]
        w = [float(x.rstrip("f")) for x in re.findall(r"-?\d[\d.eE+-]*f?", body)]
        return w, int(re.search(r"_CHANNELS (\d+)", text).group(1))
    d = json.loads(text)
    subs = [s["model"] for s in d["config"]["submodels"]] if d.get("architecture") == "SlimmableContainer" else [d]
    for m in subs:
        if m.get("architecture") == "WaveNet" and m["config"]["layers"][0]["channels"] == channels:
            return m["weights"], channels
    raise SystemExit(f"no {channels}-channel WaveNet in {path}")


def _layers(w, c, simd):
    """Weight stream -> (per-layer blocks in the kernel's order, head, head bias, scale)."""
    expect = c + sum(c * c * k + c + c + c * c + c for k in KERNEL) + c * HEAD_TAPS + 2
    if len(w) != expect:
        raise SystemExit(f"{len(w)} weights, an A2 model with {c} channels has {expect}")
    if simd and c & 1:
        raise SystemExit("the SIMD layout needs an even channel count")
    o = c                                             # stream offset, after the rechannel weights
    blocks, pres = [], []
    for k in KERNEL:
        conv, o = o, o + c * c * k
        bias, o = o, o + c
        mixin, o = o, o + c
        w1, o = o, o + c * c
        b1, o = o, o + c
        rows = [[w[conv + (i * c + j) * k + t] for t in range(k) for j in range(c)] for i in range(c)]
        res = [[w[w1 + i * c + j] for j in range(c)] for i in range(c)]
        blk, pre = [], []
        if simd:                                      # channel pairs interleaved, a spare pair after
            for i in range(0, c, 2):                  # each convolution row; mix-in and bias apart
                for u, v in zip(rows[i] + [0.0], rows[i + 1] + [0.0]):
                    blk += [u, v]
                pre += [w[mixin + i], w[mixin + i + 1], w[bias + i], w[bias + i + 1]]
            for i in range(0, c, 2):
                for u, v in zip(res[i] + [w[b1 + i]], res[i + 1] + [w[b1 + i + 1]]):
                    blk += [u, v]
        else:
            for i in range(c):
                blk += [w[bias + i], w[mixin + i]] + rows[i]
            for i in range(c):
                blk += [w[b1 + i]] + res[i]
        blocks.append(blk)
        pres.append(pre)
    head = [w[o + j * HEAD_TAPS + t] for t in range(HEAD_TAPS) for j in range(c)]
    return (blocks, pres) if simd else blocks, head, w[o + c * HEAD_TAPS], w[o + c * HEAD_TAPS + 1]


def pack(w, c, base):
    """Model block words for nam_a2.asm: one run of memory at `base`."""
    n = len(KERNEL)
    blocks, head, hbias, scale = _layers(w, c, False)
    head = [hbias] + head
    a_table = base + 9
    a_rech = a_table + 7 * n
    a_head = a_rech + c
    a_w = a_head + len(head)
    a_ring = a_w + sum(len(b) for b in blocks)
    table, words = [], []
    ring = a_ring + HEAD_TAPS * c                     # head ring first, then the layers'
    for k, d, blk in zip(KERNEL, DILATION, blocks):
        ln = ((k - 1) * d + 1) * c
        table += [ring, ln, ring, (d - 1) * c, k, k * c, a_w + len(words)]
        words += blk
        ring += ln
    out = [c, n, a_table, a_rech, a_head, f2b(scale), a_ring, HEAD_TAPS * c, a_ring]
    out += table + [f2b(v) for v in w[:c]] + [f2b(v) for v in head] + [f2b(v) for v in words]
    return out + [0] * (ring - a_ring)


# Internal memory of the ADSP-21469 as 32-bit words. A read through DM and one
# through PM in the same instruction want to be in different blocks.
# nam_a2_simd.asm's dot loop reads a layer's ring through DM and its weights
# through PM, and the 1x1 reads the scratch area in block 2 with the weights,
# so rings and weights go into blocks 3 and 1, a layer's ring never in the
# block of its weights. The top 16,000 words of block 1 are left for
# nam_live.asm's delay.
BLOCK1 = (0xB2000, 0xBA000)
BLOCK3 = (0xE0000, 0xE8000)


def ring_places(k, d):
    """Places in a layer's ring and how many a write moves on: see the kernel."""
    return (k - 1) * d + 1, (k - 1) * (d - 1) + 1


def place(w, c, model, x=None, pools=(BLOCK3, BLOCK1)):
    """Lay a model out for nam_a2_simd.asm. `model` is the kernel's MODEL.
    Returns ([(address, words)] to load, [(base, length)] of every ring). The
    rings are not loaded: nam_reset clears them."""
    n = len(KERNEL)
    (blocks, pres), head, hbias, scale = _layers(w, c, True)
    blocks[-1] = blocks[-1] + [0, 0]                  # the loops read one pair ahead
    lens = [ring_places(k, d)[0] * c for k, d in zip(KERNEL, DILATION)]
    first = [f2b(v) for v in w[:c] + head] + [0, 0]

    # Which pool gets each layer's ring (its weights go to the other): the
    # largest rings first, each to the side with more room left, backing up
    # when something does not fit.
    cap = [b - a for a, b in pools]
    order = sorted(range(n), key=lambda i: -lens[i])
    side = {}

    def fit(j, used):
        if j == n:
            return any(u + len(first) + c + 2 <= cp for u, cp in zip(used, cap))
        i = order[j]
        need = lens[i] + (lens[i] & 1), len(blocks[i]) + (len(blocks[i]) & 1)
        for p in sorted((0, 1), key=lambda p: used[p] - cap[p]):
            u = list(used)
            u[p] += need[0]
            u[1 - p] += need[1]
            if u[0] <= cap[0] and u[1] <= cap[1]:
                side[i] = p
                if fit(j + 1, u):
                    return True
        return False

    if not fit(0, [0, 0]):
        raise SystemExit("no room for the model in " + ", ".join(f"{a:#x}..{b - 1:#x}" for a, b in pools))
    free = [list(p) for p in pools]

    def alloc(ln, p=None):                            # even addresses: a pair never straddles a ring's end
        for q in ((p,) if p is not None else (0, 1)):
            a = free[q][0] + (free[q][0] & 1)
            if a + ln <= free[q][1]:
                free[q][0] = a + ln
                return a
        raise SystemExit(f"no room for {ln} more words")

    rings = {i: alloc(lens[i], side[i]) for i in order}
    a_w = {i: alloc(len(blocks[i]), 1 - side[i]) for i in order}
    a_rech = alloc(len(first))
    a_head = a_rech + c
    rings[n] = alloc(c)                               # where the final 1x1 output goes
    lens.append(c)
    step = [2 + (ring_places(k, d)[1] - 1) * c for k, d in zip(KERNEL, DILATION)] + [2]
    a_table = model + 11
    table = [rings[0], lens[0], step[0], rings[0], 0]
    for i, k in enumerate(KERNEL):
        table += [k * c, -(k * c + 1) & 0xFFFFFFFF, a_w[i]] + [f2b(v) for v in pres[i]]
        table += [rings[i + 1], lens[i + 1], step[i + 1], rings[i + 1], 0]
    hring = a_table + len(table)
    hring += hring & 1
    hdr = [c, n, a_table, a_rech, a_head, f2b(scale), hring, HEAD_TAPS * c, hring, 0, f2b(hbias)]
    return ([(model, hdr + table), (a_rech, first)] + [(a_w[i], [f2b(v) for v in blocks[i]]) for i in range(n)],
            [(hring, HEAD_TAPS * c)] + [(rings[i], lens[i]) for i in range(n + 1)])


def main():
    a = sys.argv[1:]
    opt = {}
    for flag in ("--channels", "--base", "-o"):
        if flag in a:
            i = a.index(flag)
            opt[flag] = a[i + 1]
            del a[i:i + 2]
    if len(a) != 1:
        sys.exit(__doc__)
    w, c = load(a[0], int(opt.get("--channels", 3)))
    blob = pack(w, c, int(opt.get("--base", "0xc0100"), 0))
    if "-o" in opt:
        open(opt["-o"], "wb").write(struct.pack("<%dI" % len(blob), *blob))
    print(f"{c} channels, {len(w)} weights -> {len(blob)} words ({len(blob) - len(w)} of them state and tables)")


if __name__ == "__main__":
    main()
