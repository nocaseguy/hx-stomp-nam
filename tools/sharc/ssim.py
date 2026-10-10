#!/usr/bin/env python3
"""ssim - a small simulator for the 48-bit SHARC code we write ourselves.

It executes the encoded instructions sasm produces, so what is tested is what
ships. It covers the instruction forms and compute operations our own sources
use and stops with SimError on anything else, rather than guessing.

Model: 32-bit IEEE floats (the code must run with RND32 set, round to
nearest), circular buffers on post-modify and MODIFY when L != 0, hardware
loops, the PC stack, AZ/AN and the bit-test flag. SIMD (MODE1 PEYEN): computes
run on both processing elements, a data register moved through an index
register pairs with its S register and the next memory word, a store of any
other register also overwrites the next memory word (with junk), and loads through
I1 / I9 are broadcast when BDCST1 / BDCST9 is set; every other use of a data
register in SIMD stops the simulation. Flags are those of PEx. The core
timer counts down one per instruction while MODE2 TIMEN is set (no reload, no
interrupt), and every change of FLAGS is logged in `flag_log` as (cycles,
value). Not modelled: delayed branches, interrupts, 40-bit floats, saturation,
pipeline effects and the restrictions on short loops. `cycles` counts
instructions executed, which is the cycle count of straight-line single-cycle
code.
"""
import struct

M32 = 0xFFFFFFFF
U_PC, U_PCSTK, U_CURLCNTR, U_LCNTR, U_TCOUNT = 0x63, 0x64, 0x67, 0x68, 0x6F
U_MODE1, U_MODE2, U_FLAGS = 0x72, 0x74, 0x75
PEYEN, BDCST9, BDCST1 = 1 << 21, 1 << 22, 1 << 23
JUNK = 0x7FC0DEAD                # what a SIMD store of an unpaired register leaves in the next word
TIMEN = 1 << 5


class SimError(Exception):
    pass


def f2b(x):
    try:
        return struct.unpack("<I", struct.pack("<f", x))[0]
    except OverflowError:
        return 0x7F800000 if x > 0 else 0xFF800000


def b2f(b):
    return struct.unpack("<f", struct.pack("<I", b & M32))[0]


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def sx(v, bits):
    return v - (1 << bits) if v >> (bits - 1) & 1 else v


class Sim:
    def __init__(self):
        self.u = [0] * 128                  # universal registers by code
        self.dm = {}                        # 32-bit words by normal-word address
        self.pm = {}                        # 48-bit instructions by address
        self.pc = 0
        self.pcstk, self.loops = [], []     # loops: [top, end, count]
        self.az = self.an = self.tf = False
        self.cycles = 0
        self.flag_log = []

    # ---------------------------------------------------------- loading
    def load(self, sections):
        for s in sections:
            mem = self.pm if s.kind == "pm" else self.dm
            for i, w in enumerate(s.words):
                mem[s.addr + i] = w

    def rd(self, a):
        return self.dm.get(a & M32, 0)

    def wr(self, a, v):
        self.dm[a & M32] = v & M32

    # ---------------------------------------------------------- registers
    def get(self, code):
        if code == U_PC:
            return self.pc
        return self.u[code]

    def set(self, code, v):
        v &= M32
        self.u[code] = v
        if 0x40 <= code < 0x50:             # loading Bn loads In
            self.u[code - 0x30] = v

    def R(self, n):
        return self.u[n]

    def F(self, n):
        return b2f(self.u[n])

    def setR(self, n, v):
        self.u[n] = v & M32
        self.az, self.an = self.u[n] == 0, bool(self.u[n] >> 31)

    def setF(self, n, x):
        self.u[n] = f2b(x)
        x = b2f(self.u[n])
        self.az, self.an = x == 0, x < 0

    # ---------------------------------------------------------- address generators
    def _wrap(self, i, v):
        b, ln = self.u[0x40 + i], self.u[0x30 + i]
        if ln:
            if v >= b + ln:
                v -= ln
            elif v < b:
                v += ln
        return v & M32

    def post(self, i, step):
        a = self.u[0x10 + i]
        self.u[0x10 + i] = self._wrap(i, a + s32(step))
        return a

    # ---------------------------------------------------------- SIMD
    @property
    def simd(self):
        return bool(self.u[U_MODE1] & PEYEN)

    def _scalar(self, *codes):
        if self.simd and any(c < 16 or 0x50 <= c < 0x60 for c in codes):
            raise SimError(f"this data-register move in SIMD mode is not modelled (pc {self.pc:#x})")

    def dget(self, r):
        """Value(s) of data register r for a store through an index register."""
        return (self.u[r], self.u[0x50 + r]) if self.simd else (self.u[r],)

    def dload(self, a, i):
        """Value(s) for a data register loaded from address a through index register i."""
        if not self.simd:
            return (self.rd(a),)
        if (i == 1 and self.u[U_MODE1] & BDCST1) or (i == 9 and self.u[U_MODE1] & BDCST9):
            return (self.rd(a), self.rd(a))
        return (self.rd(a), self.rd(a + 1))

    def dset(self, r, v):
        self.u[r] = v[0]
        if len(v) > 1:
            self.u[0x50 + r] = v[1]

    def dstore(self, a, v):
        for k, x in enumerate(v):
            self.wr(a + k, x)

    def ustore(self, a, v):
        """Store a register that has no partner. In SIMD the processor writes the
        next word as well (seen on the unit: a zero); nothing may depend on it."""
        self.wr(a, v)
        if self.simd:
            self.wr(a + 1, JUNK)

    def compute(self, c):
        self.compute1(c)
        if c and self.simd:
            flags = self.az, self.an
            self.u[0:16], self.u[0x50:0x60] = self.u[0x50:0x60], self.u[0:16]
            self.compute1(c)
            self.u[0:16], self.u[0x50:0x60] = self.u[0x50:0x60], self.u[0:16]
            self.az, self.an = flags

    # ---------------------------------------------------------- conditions
    def cond(self, c):
        if c == 31:
            return True
        neg, base = c >= 16, c & 15
        v = {0: self.az, 1: self.an, 2: self.an or self.az, 13: self.tf}.get(base)
        if v is None:
            raise SimError(f"condition {c} is not modelled")
        return v != neg

    # ---------------------------------------------------------- compute
    def compute1(self, c):
        if c == 0:
            return
        n, x, y = c >> 8 & 15, c >> 4 & 15, c & 15
        if c >> 22:                                           # multifunction
            code = c >> 16 & 0x3F
            m, a = c >> 12 & 15, n
            xm, ym, xa, ya = c >> 6 & 3, 4 + (c >> 4 & 3), 8 + (c >> 2 & 3), 12 + (c & 3)
            if code in (0x18, 0x19):
                p = self.F(xm) * self.F(ym)
                s = self.F(xa) + self.F(ya) if code == 0x18 else self.F(xa) - self.F(ya)
                self.u[m] = f2b(p)
                self.setF(a, s)
                return
            raise SimError(f"multifunction compute {code:#x} is not modelled")
        cu, op = c >> 20 & 3, c >> 12 & 0xFF
        if cu == 0:
            if c >> 16 & 15 in (0x7, 0xF) and op >> 4 in (0x7, 0xF):       # dual add/subtract
                s = c >> 12 & 15
                if op >> 4 == 0xF:
                    a, d = self.F(x) + self.F(y), self.F(x) - self.F(y)
                    self.u[s] = f2b(d)
                    self.setF(n, a)
                else:
                    a, d = self.R(x) + self.R(y), self.R(x) - self.R(y)
                    self.u[s] = d & M32
                    self.setR(n, a)
                return
            rx, ry, fx, fy = self.R(x), self.R(y), self.F(x), self.F(y)
            if op == 0x01: self.setR(n, rx + ry)
            elif op == 0x02: self.setR(n, rx - ry)
            elif op == 0x0A: self.az, self.an = s32(rx) == s32(ry), s32(rx) < s32(ry)
            elif op == 0x29: self.setR(n, rx + 1)
            elif op == 0x2A: self.setR(n, rx - 1)
            elif op == 0x22: self.setR(n, -rx)
            elif op == 0x21: self.setR(n, rx)
            elif op == 0x40: self.setR(n, rx & ry)
            elif op == 0x41: self.setR(n, rx | ry)
            elif op == 0x42: self.setR(n, rx ^ ry)
            elif op == 0x43: self.setR(n, ~rx)
            elif op == 0x61: self.setR(n, min(s32(rx), s32(ry)))
            elif op == 0x62: self.setR(n, max(s32(rx), s32(ry)))
            elif op == 0x81: self.setF(n, fx + fy)
            elif op == 0x82: self.setF(n, fx - fy)
            elif op == 0x8A: self.az, self.an = fx == fy, fx < fy
            elif op == 0xA2: self.setF(n, -fx)
            elif op == 0xB0: self.setF(n, abs(fx))
            elif op == 0xA1: self.setF(n, fx)
            elif op == 0xCA: self.setF(n, float(s32(rx)))
            elif op == 0xC9: self.setR(n, int(round(fx)))
            elif op == 0xDA: self.setF(n, float(s32(rx)) * 2.0 ** s32(ry))
            elif op == 0xD9: self.setR(n, max(-1 << 31, min((1 << 31) - 1, int(round(fx * 2.0 ** s32(ry))))))
            elif op == 0xE1: self.setF(n, min(fx, fy))
            elif op == 0xE2: self.setF(n, max(fx, fy))
            else: raise SimError(f"ALU operation {op:#x} is not modelled")
        elif cu == 1:
            if op == 0x30: self.setF(n, self.F(x) * self.F(y))
            elif op == 0x70: self.setR(n, s32(self.R(x)) * s32(self.R(y)))
            else: raise SimError(f"multiplier operation {op:#x} is not modelled")
        else:
            k = s32(self.R(y))
            if op == 0x00: self.setR(n, self.R(x) << k if k >= 0 else self.R(x) >> -k)
            elif op == 0x04: self.setR(n, s32(self.R(x)) << k if k >= 0 else s32(self.R(x)) >> -k)
            else: raise SimError(f"shifter operation {op:#x} is not modelled")

    def shimm(self, w):
        self._scalar(0)
        op6, k = w >> 16 & 0x3F, sx((w >> 27 & 15) << 8 | w >> 8 & 0xFF, 12)
        n, x = w >> 4 & 15, w & 15
        if op6 == 0: self.setR(n, self.R(x) << k if k >= 0 else self.R(x) >> -k)
        elif op6 == 1: self.setR(n, s32(self.R(x)) << k if k >= 0 else s32(self.R(x)) >> -k)
        else: raise SimError(f"immediate shift {op6:#x} is not modelled")

    # ---------------------------------------------------------- one instruction
    def step(self):
        pc = self.pc
        if pc not in self.pm:
            raise SimError(f"no code at {pc:#x}")
        w = self.pm[pc]
        self.cycles += 1
        if self.u[U_MODE2] & TIMEN:
            self.u[U_TCOUNT] = (self.u[U_TCOUNT] - 1) & M32
        nxt = pc + 1
        top, op8, cc, cmp_ = w >> 45, w >> 40, w >> 33 & 31, w & 0x7FFFFF

        def branch(target, call, db):
            nonlocal nxt
            if db:
                raise SimError("delayed branches are not modelled")
            if call:
                self.pcstk.append(pc + 1)
            nxt = target & 0xFFFFFF

        if top == 1:                                          # compute, DM and PM moves
            di, dmm, dr = w >> 41 & 7, w >> 38 & 7, w >> 33 & 15
            pi, pmm, pr = 8 + (w >> 30 & 7), 8 + (w >> 27 & 7), w >> 23 & 15
            dst, pst = w >> 44 & 1, w >> 37 & 1
            dv, pv = self.dget(dr), self.dget(pr)
            da, pa = self.post(di, self.u[0x20 + dmm]), self.post(pi, self.u[0x20 + pmm])
            dl = None if dst else self.dload(da, di)
            pl = None if pst else self.dload(pa, pi)
            self.compute(cmp_)
            if dst: self.dstore(da, dv)
            else: self.dset(dr, dl)
            if pst: self.dstore(pa, pv)
            else: self.dset(pr, pl)
        elif top == 2:                                        # ureg <-> memory through I, M
            if self.cond(cc):
                g = w >> 32 & 1
                i, m, ur = (w >> 41 & 7) + 8 * g, (w >> 38 & 7) + 8 * g, w >> 23 & 0x7F
                if w >> 30 & 1:
                    raise SimError("long-word moves are not modelled")
                if 0x10 <= ur < 0x50 and (ur & 15 >= 8) == bool(g) and (w >> 31 & 1 or w >> 44 & 1):
                    raise SimError(f"an address-generator register moved through its own generator (pc {pc:#x})")
                a = self.post(i, self.u[0x20 + m]) if w >> 44 & 1 else self.u[0x10 + i] + s32(self.u[0x20 + m])
                self._scalar(*([ur] if ur >= 16 else []))
                pair = ur < 16
                if w >> 31 & 1:
                    v = self.dget(ur) if pair else self.get(ur)
                    self.compute(cmp_)
                    self.dstore(a, v) if pair else self.ustore(a, v)
                else:
                    v = self.dload(a, i) if pair else self.rd(a)
                    self.compute(cmp_)
                    self.dset(ur, v) if pair else self.set(ur, v)
        elif top == 3 and not w >> 44 & 1:                    # dreg <-> memory, 6-bit offset
            if self.cond(cc):
                i, r, d6 = (w >> 41 & 7) + 8 * (w >> 40 & 1), w >> 23 & 15, sx(w >> 27 & 0x3F, 6)
                a = self.post(i, d6) if w >> 38 & 1 else self.u[0x10 + i] + d6
                if w >> 39 & 1:
                    v = self.dget(r)
                    self.compute(cmp_)
                    self.dstore(a, v)
                else:
                    v = self.dload(a, i)
                    self.compute(cmp_)
                    self.dset(r, v)
        elif top == 3:
            if w >> 43 & 1:
                raise SimError("register swap is not modelled")
            if self.cond(cc):                                 # ureg = ureg
                self._scalar((w >> 38 & 0x1F) << 2 | w >> 31 & 3, w >> 23 & 0x7F)
                v = self.get((w >> 38 & 0x1F) << 2 | w >> 31 & 3)
                self.compute(cmp_)
                self.set(w >> 23 & 0x7F, v)
        elif top == 4 and w >> 44 & 1:                        # memory = constant
            g = w >> 37 & 1
            self.wr(self.post((w >> 41 & 7) + 8 * g, self.u[0x20 + (w >> 38 & 7) + 8 * g]), w)
        elif top == 4:
            raise SimError("immediate shift with a move is not modelled")
        elif top == 5:                                        # ureg <-> DM(offset, I)
            self._scalar(w >> 32 & 0x7F)
            a = self.u[0x10 + (w >> 41 & 7) + 8 * (w >> 44 & 1)] + s32(w)
            if w >> 40 & 1: self.ustore(a, self.get(w >> 32 & 0x7F))
            else: self.set(w >> 32 & 0x7F, self.rd(a))
        elif top:
            raise SimError(f"instruction {w:012x} is not modelled")
        elif op8 == 0x00:
            if w >> 39 & 1:
                raise SimError("IDLE")
        elif op8 == 0x01:
            if self.cond(cc): self.compute(cmp_)
        elif op8 == 0x02:
            if self.cond(cc): self.shimm(w)
        elif op8 == 0x04:                                     # MODIFY (I, M)
            if self.cond(cc):
                g = 8 * (w >> 38 & 1)
                src = g + (w >> 30 & 7)
                self.compute(cmp_)
                v = self._wrap(src, self.u[0x10 + src] + s32(self.u[0x20 + g + (w >> 27 & 7)]))
                self.u[0x10 + (src ^ (w >> 24 & 7))] = v
        elif op8 in (0x06, 0x07):                             # direct jump / call
            if self.cond(cc):
                t = w & 0xFFFFFF
                branch(pc + sx(t, 24) if op8 & 1 else t, w >> 39 & 1, w >> 26 & 1)
        elif op8 in (0x08, 0x09):                             # indirect or short relative, with compute
            taken = self.cond(cc)
            if taken != bool(w >> 25 & 1):
                self.compute(cmp_)
            if taken:
                if op8 & 1:
                    t = pc + sx(w >> 27 & 0x3F, 6)
                else:
                    t = self.u[0x18 + (w >> 30 & 7)] + s32(self.u[0x28 + (w >> 27 & 7)])
                branch(t, w >> 39 & 1, w >> 26 & 1)
        elif op8 == 0x0A:                                     # RTS
            taken = self.cond(cc)
            if taken != bool(w >> 25 & 1):
                self.compute(cmp_)
            if taken:
                if w >> 26 & 1:
                    raise SimError("delayed branches are not modelled")
                if not self.pcstk:
                    return False                              # return from the entry call
                nxt = self.pcstk.pop()
        elif op8 in (0x0C, 0x0D):                             # counted loop
            n = w >> 24 & 0xFFFF if op8 == 0x0C else self.get(w >> 32 & 0x7F)
            if n == 0:
                raise SimError("loop count 0")
            self.u[U_LCNTR] = n
            self.loops.append([pc + 1, pc + sx(w & 0xFFFFFF, 24), n])
        elif op8 == 0x0F:
            self._scalar(w >> 32 & 0x7F)
            self.set(w >> 32 & 0x7F, w)
        elif 0x10 <= op8 <= 0x13:                             # ureg <-> DM(address)
            if w >> 39 & 1:
                raise SimError("long-word moves are not modelled")
            self._scalar(w >> 32 & 0x7F)
            if op8 & 1: self.ustore(w, self.get(w >> 32 & 0x7F))
            else: self.set(w >> 32 & 0x7F, self.rd(w))
        elif op8 == 0x14:                                     # BIT
            r, d, bop = 0x70 + (w >> 32 & 15), w & M32, w >> 37 & 7
            v = self.u[r]
            if bop == 0: self.u[r] = v | d
            elif bop == 1: self.u[r] = v & ~d
            elif bop == 2: self.u[r] = v ^ d
            elif bop == 4: self.tf = v & d == d
            elif bop == 5: self.tf = v == d
            if r == U_FLAGS and self.u[r] != v:
                self.flag_log.append((self.cycles, self.u[r]))
        elif op8 == 0x16 and not w >> 39 & 1:                 # MODIFY (I, constant)
            src = 8 * (w >> 38 & 1) + (w >> 32 & 7)
            self.u[0x10 + (src ^ (w >> 35 & 7))] = self._wrap(src, self.u[0x10 + src] + s32(w))
        else:
            raise SimError(f"instruction {w:012x} at {pc:#x} is not modelled")

        if self.loops and self.loops[-1][1] == pc and nxt == pc + 1:
            lp = self.loops[-1]
            lp[2] -= 1
            if lp[2]:
                nxt = lp[0]
            else:
                self.loops.pop()
        self.pc = nxt
        return True

    def call(self, entry, limit=50_000_000):
        """Run from `entry` until its RTS; returns the instructions executed."""
        self.pc, self.pcstk, self.loops = entry, [], []
        start = self.cycles
        while self.step():
            if self.cycles - start > limit:
                raise SimError(f"no return after {limit} instructions (pc {self.pc:#x})")
        return self.cycles - start
