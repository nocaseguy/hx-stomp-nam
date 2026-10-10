#!/usr/bin/env python3
"""sasm - a small assembler for 48-bit ADSP-214xx SHARC (ISA) code.

There is no ADI toolchain in this project, so SHARC code is written with this
and checked by encoding the disassembly of known code back to its bytes
(tools/sharc/sasm_selftest.py).

Syntax is ADI-like; the display text of the ghidra-sharc disassembler is also
accepted (a leading MOVE/COMP/PAR/SHIFT/SWAP word is ignored), which is what
makes the round-trip check possible.

    .section pm 0x8c100        code (48-bit words), address in normal words
    .section dm 0xc0000        data (32-bit words)
    .equ N, 16                 constant
    .word 1, 2, sym+3          32-bit data words (48-bit in a pm section)
    .float 1.0, -0.5           IEEE single data words
    .space 64                  reserve zero words
    label:
        R0 = DM(I0, M1), R4 = PM(I8, M9);
        F8 = F0 * F4, F12 = F8 + F12;
        IF EQ JUMP done (DB);          absolute;  JUMP (PC, done) is relative
        LCNTR = N, DO last UNTIL LCE;  `last` labels the loop's final instruction
        R0 = 1.5;                      float immediates need a decimal point

The opcode tables are adapted from tools/sharc_isa.py of the ghidra-sharc
extension (Apache License 2.0), which transcribes them from the ADI "SHARC
Processor Programming Reference".
"""
import re
import struct
import sys

# ------------------------------------------------------------------ tables
_MISC6 = ["FADDR", "DADDR", "UREG_62", "PC", "PCSTK", "PCSTKP", "LADDR", "CURLCNTR",
          "LCNTR", "EMUCLK", "EMUCLK2", "PX", "PX1", "PX2", "TPERIOD", "TCOUNT"]
_SREG7 = ["USTAT1", "USTAT2", "MODE1", "MMASK", "MODE2", "FLAGS", "ASTATX", "ASTATY",
          "STKYX", "STKYY", "IRPTL", "IMASK", "IMASKP", "LRPTL", "USTAT3", "USTAT4"]
UREG = ([f"R{i}" for i in range(16)] + [f"I{i}" for i in range(16)] + [f"M{i}" for i in range(16)]
        + [f"L{i}" for i in range(16)] + [f"B{i}" for i in range(16)] + [f"S{i}" for i in range(16)]
        + _MISC6 + _SREG7)
UREG_CODE = {n: i for i, n in enumerate(UREG)}
UREG_CODE.update({f"F{i}": i for i in range(16)})

COND = ["EQ", "LT", "LE", "AC", "AV", "MV", "MS", "SV", "SZ", "FLAG0_IN", "FLAG1_IN", "FLAG2_IN",
        "FLAG3_IN", "TF", "BM", "NOT LCE", "NE", "GE", "GT", "NOT AC", "NOT AV", "NOT MV", "NOT MS",
        "NOT SV", "NOT SZ", "NOT FLAG0_IN", "NOT FLAG1_IN", "NOT FLAG2_IN", "NOT FLAG3_IN", "NOT TF",
        "NOT BM", "TRUE"]
TERM = COND[:15] + ["LCE"] + COND[16:31] + ["FOREVER"]
BOP = {"SET": 0, "CLR": 1, "TGL": 2, "TST": 4, "XOR": 5}
MRREG = {0: "MR0F", 1: "MR1F", 2: "MR2F", 4: "MR0B", 5: "MR1B", 6: "MR2B"}
STACKOP = ["PUSH LOOP", "POP LOOP", "PUSH STS", "POP STS", "PUSH PCSTK", "POP PCSTK", "FLUSH CACHE"]

ALU = {0x01: "R{n} = R{x} + R{y}", 0x02: "R{n} = R{x} - R{y}", 0x05: "R{n} = R{x} + R{y} + CI",
       0x06: "R{n} = R{x} - R{y} + CI - 1", 0x09: "R{n} = (R{x} + R{y})/2",
       0x0A: "COMP(R{x}, R{y})", 0x0B: "COMPU(R{x}, R{y})", 0x25: "R{n} = R{x} + CI",
       0x26: "R{n} = R{x} + CI - 1", 0x29: "R{n} = R{x} + 1", 0x2A: "R{n} = R{x} - 1",
       0x22: "R{n} = -R{x}", 0x30: "R{n} = ABS R{x}", 0x21: "R{n} = PASS R{x}",
       0x40: "R{n} = R{x} AND R{y}", 0x41: "R{n} = R{x} OR R{y}", 0x42: "R{n} = R{x} XOR R{y}",
       0x43: "R{n} = NOT R{x}", 0x61: "R{n} = MIN(R{x}, R{y})", 0x62: "R{n} = MAX(R{x}, R{y})",
       0x63: "R{n} = CLIP R{x} BY R{y}",
       0x81: "F{n} = F{x} + F{y}", 0x82: "F{n} = F{x} - F{y}", 0x91: "F{n} = ABS (F{x} + F{y})",
       0x92: "F{n} = ABS (F{x} - F{y})", 0x89: "F{n} = (F{x} + F{y})/2", 0x8A: "COMP(F{x}, F{y})",
       0xA2: "F{n} = -F{x}", 0xB0: "F{n} = ABS F{x}", 0xA1: "F{n} = PASS F{x}",
       0xA5: "F{n} = RND F{x}", 0xBD: "F{n} = SCALB F{x} BY R{y}", 0xAD: "R{n} = MANT F{x}",
       0xC1: "R{n} = LOGB F{x}", 0xD9: "R{n} = FIX F{x} BY R{y}", 0xC9: "R{n} = FIX F{x}",
       0xDD: "R{n} = TRUNC F{x} BY R{y}", 0xCD: "R{n} = TRUNC F{x}",
       0xDA: "F{n} = FLOAT R{x} BY R{y}", 0xCA: "F{n} = FLOAT R{x}", 0xC4: "F{n} = RECIPS F{x}",
       0xC5: "F{n} = RSQRTS F{x}", 0xE0: "F{n} = F{x} COPYSIGN F{y}",
       0xE1: "F{n} = MIN(F{x}, F{y})", 0xE2: "F{n} = MAX(F{x}, F{y})",
       0xE3: "F{n} = CLIP F{x} BY F{y}"}

# key = 8-bit opcode; the immediate form (type 6) uses opcode >> 2
SHIFT = {0x00: "R{n} = LSHIFT R{x} BY {y}", 0x20: "R{n} = R{n} OR LSHIFT R{x} BY {y}",
         0x04: "R{n} = ASHIFT R{x} BY {y}", 0x24: "R{n} = R{n} OR ASHIFT R{x} BY {y}",
         0x08: "R{n} = ROT R{x} BY {y}", 0xC4: "R{n} = BCLR R{x} BY {y}", 0xC0: "R{n} = BSET R{x} BY {y}",
         0xC8: "R{n} = BTGL R{x} BY {y}", 0xCC: "BTST R{x} BY {y}", 0x44: "R{n} = FDEP R{x} BY {f}",
         0x4C: "R{n} = FDEP R{x} BY {f} (SE)", 0x64: "R{n} = R{n} OR FDEP R{x} BY {f}",
         0x6C: "R{n} = R{n} OR FDEP R{x} BY {f} (SE)", 0x40: "R{n} = FEXT R{x} BY {f}",
         0x48: "R{n} = FEXT R{x} BY {f} (SE)", 0x80: "R{n} = EXP R{x}", 0x84: "R{n} = EXP R{x} (EX)",
         0x88: "R{n} = LEFTZ R{x}", 0x8C: "R{n} = LEFTO R{x}", 0x90: "R{n} = FPACK F{x}",
         0x94: "F{n} = FUNPACK R{x}"}

MOD1 = {(1, 1, 0, 0): "SSI", (0, 1, 0, 0): "SUI", (1, 0, 0, 0): "USI", (0, 0, 0, 0): "UUI",
        (1, 1, 1, 0): "SSF", (0, 1, 1, 0): "SUF", (1, 0, 1, 0): "USF", (0, 0, 1, 0): "UUF",
        (1, 1, 1, 1): "SSFR", (0, 1, 1, 1): "SUFR", (1, 0, 1, 1): "USFR", (0, 0, 1, 1): "UUFR"}

_AS = ["R{a} = R{xa} + R{ya}", "R{a} = R{xa} - R{ya}", "R{a} = (R{xa} + R{ya})/2"]
MULALU = {}
for _base, _mul in ((0x04, "R{m} = R{xm} * R{ym} (SSFR)"), (0x08, "MRF = MRF + R{xm} * R{ym} (SSF)"),
                    (0x0C, "R{m} = MRF + R{xm} * R{ym} (SSFR)"), (0x10, "MRF = MRF - R{xm} * R{ym} (SSF)"),
                    (0x14, "R{m} = MRF - R{xm} * R{ym} (SSFR)")):
    for _k, _a in enumerate(_AS):
        MULALU[_base + _k] = f"{_mul}, {_a}"
for _k, _a in enumerate(["F{a} = F{xa} + F{ya}", "F{a} = F{xa} - F{ya}", "F{a} = FLOAT R{xa} BY R{ya}",
                         "R{a} = FIX F{xa} BY R{ya}", "F{a} = (F{xa} + F{ya})/2", "F{a} = ABS F{xa}",
                         "F{a} = MAX(F{xa}, F{ya})", "F{a} = MIN(F{xa}, F{ya})"]):
    MULALU[0x18 + _k] = "F{m} = F{xm} * F{ym}, " + _a


def mult_text(op):
    """ADI text of multiplier opcode `op` with {n} {x} {y} placeholders, or None."""
    if op == 0x30:
        return "F{n} = F{x} * F{y}"
    hi = op >> 6
    dst = (op >> 1) & 3
    if hi == 0:
        src = "MRB" if dst & 1 else "MRF"
        lhs = {0: "R{n}", 1: "R{n}", 2: "MRF", 3: "MRB"}[dst]
        if (op >> 4) == 0:
            return None if op == 0 else f"{lhs} = SAT {src} ({'S' if op & 1 else 'U'}{'F' if op & 8 else 'I'})"
        if (op >> 4) == 1:
            if op == 0x14:
                return "MRF = 0"
            if op == 0x16:
                return "MRB = 0"
            if op & 0x08:
                return f"{lhs} = RND {src} ({'S' if op & 1 else 'U'}F)"
        return None
    mod = MOD1.get(((op >> 5) & 1, (op >> 4) & 1, (op >> 3) & 1, op & 1))
    if mod is None:
        return None
    if hi == 1:
        lhs = {0: "R{n}", 1: None, 2: "MRF", 3: "MRB"}[dst]
        return f"{lhs} = R{{x}} * R{{y}} ({mod})" if lhs else None
    acc = "MRB" if dst & 1 else "MRF"
    lhs = "R{n}" if dst < 2 else acc
    return f"{lhs} = {acc} {'+' if hi == 2 else '-'} R{{x}} * R{{y}} ({mod})"


# ------------------------------------------------------------------ text handling
class AsmError(Exception):
    pass


def canon(s):
    """Upper-case and drop every space that does not separate two words."""
    s = re.sub(r"\s+", " ", s.strip().upper())
    return re.sub(r" ?([^\w ]) ?", r"\1", s)


def _rx(template, free=()):
    """Regex for a canonical template; a repeated placeholder must repeat and
    the placeholders in `free` take any text instead of a register number."""
    seen = set()

    def sub(m):
        k = m.group(1).lower()
        if k in seen:
            return f"(?P={k})"
        seen.add(k)
        return f"(?P<{k}>[^,()]+)" if k in free else f"(?P<{k}>\\d+)"
    parts = re.split(r"(QQ[A-Z0-9]+?QQ)", canon(re.sub(r"\{(\w+)\}", r"QQ\1QQ", template)))
    out = "".join(sub(re.match(r"QQ(\w+?)QQ", p)) if p.startswith("QQ") else re.escape(p).replace("\\ ", " ?")
                  for p in parts)
    return re.compile("^" + out + "$")


_LIMIT = {"xm": 0, "ym": 4, "xa": 8, "ya": 12}


def _build_compute():
    t = []                                             # (regex, fn(groups) -> 23-bit field)

    def std(cu, op):
        return lambda g: (cu << 20 | op << 12 | g.get("n", 0) << 8 | g.get("x", 0) << 4 | g.get("y", 0))
    for op, tmpl in ALU.items():
        t.append((_rx(tmpl), std(0, op)))
    for hi4, p in ((0x7, "R"), (0xF, "F")):
        t.append((_rx(f"{p}{{n}} = {p}{{x}} + {p}{{y}}, {p}{{s}} = {p}{{x}} - {p}{{y}}"),
                  lambda g, hi4=hi4: hi4 << 16 | g["s"] << 12 | g["n"] << 8 | g["x"] << 4 | g["y"]))
    for op in range(256):
        tmpl = mult_text(op)
        if tmpl:
            t.append((_rx(tmpl), std(1, op)))
    for op, tmpl in SHIFT.items():
        if "{f}" in tmpl:
            tmpl = tmpl.replace("{f}", "R{y}")
        t.append((_rx(tmpl.replace("{y}", "R{y}")), std(2, op)))

    def mf(code):
        return lambda g: (1 << 22 | code << 16 | g.get("m", 0) << 12 | g["a"] << 8 |
                          g["xm"] << 6 | g["ym"] - 4 << 4 | g["xa"] - 8 << 2 | g.get("ya", 12) - 12)
    for code, tmpl in MULALU.items():
        t.append((_rx(tmpl), mf(code)))
    for fl, tmpl in ((0, "R{m} = R{xm} * R{ym} (SSFR), R{a} = R{xa} + R{ya}, R{s} = R{xa} - R{ya}"),
                     (1, "F{m} = F{xm} * F{ym}, F{a} = F{xa} + F{ya}, F{s} = F{xa} - F{ya}")):
        t.append((_rx(tmpl), lambda g, fl=fl: (1 << 22 | (2 | fl) << 20 | g["s"] << 16 | g["m"] << 12 | g["a"] << 8 |
                                               g["xm"] << 6 | g["ym"] - 4 << 4 | g["xa"] - 8 << 2 | g["ya"] - 12)))
    for code, mr in MRREG.items():
        t.append((_rx(f"R{{n}} = {mr}"), lambda g, c=code: 1 << 22 | c << 12 | g["n"] << 8))
        t.append((_rx(f"{mr} = R{{n}}"), lambda g, c=code: 1 << 22 | 1 << 16 | c << 12 | g["n"] << 8))
    return t


_COMPUTE = _build_compute()
_SHIMM = [(_rx(tmpl, ("y", "f")), op >> 2, "{f}" in tmpl) for op, tmpl in SHIFT.items() if "{y}" in tmpl or "{f}" in tmpl]


def _ints(m):
    g = {}
    for k, v in m.groupdict().items():
        if v is None:
            continue
        g[k] = int(v)
        if g[k] > 15:
            return None
    for k, base in _LIMIT.items():
        if k in g and not base <= g[k] < base + 4:
            return None
    return g


def compute_fields(text):
    """All 23-bit compute encodings of canonical `text`."""
    out = []
    for rx, fn in _COMPUTE:
        m = rx.match(text)
        if m:
            g = _ints(m)
            if g is not None:
                out.append(fn(g))
    return out


class Ctx:
    def __init__(self, pc=0, syms=None, final=True):
        self.pc, self.syms, self.final = pc, syms or {}, final

    def val(self, expr):
        e = expr.strip()
        if re.fullmatch(r"-?\d+\.\d*(E[+-]?\d+)?|-?\d+E[+-]?\d+", e):
            return struct.unpack("<I", struct.pack("<f", float(e)))[0]
        if not re.fullmatch(r"[\w\s+\-*/()<>&|~]+", e):
            raise AsmError(f"bad expression '{expr}'")

        def sym(m):
            w = m.group(0)
            if re.fullmatch(r"0X[0-9A-F]+", w):
                return str(int(w, 16))
            if w.isdigit():
                return str(int(w, 10))
            if w in self.syms:
                return str(self.syms[w])
            if self.final:
                raise AsmError(f"undefined symbol '{w}'")
            return "0"
        try:
            return int(eval(re.sub(r"\w+", sym, e).replace("/", "//"), {"__builtins__": {}}))
        except AsmError:
            raise
        except Exception:
            raise AsmError(f"bad expression '{expr}'")


def _signed(v, bits, what):
    v &= 0xFFFFFFFF
    if v & 0x80000000:
        v -= 1 << 32
    if not -(1 << (bits - 1)) <= v < (1 << (bits - 1)):
        raise AsmError(f"{what} {v} does not fit {bits} bits")
    return v & ((1 << bits) - 1)


def shimm_fields(text, ctx):
    """(dataex, 23-bit field) encodings of an immediate shift."""
    out = []
    for rx, op6, field in _SHIMM:
        m = rx.match(text)
        if not m:
            continue
        g = m.groupdict()
        n, x = int(g.get("n") or 0), int(g["x"])
        if n > 15 or x > 15:
            continue
        arg = g.get("y") or g.get("f")
        if re.fullmatch(r"R\d+", arg):
            continue
        try:
            if field:
                b, ln = (ctx.val(p) for p in arg.split(":"))
                if not (0 <= b < 64 and 0 <= ln < 64):
                    raise AsmError("bit field out of range")
                d8, ex = (ln & 3) << 6 | b, ln >> 2
            else:
                v = _signed(ctx.val(arg), 12, "shift count")
                d8, ex = v & 0xFF, v >> 8
        except ValueError:
            continue
        out.append((ex, op6 << 16 | d8 << 8 | n << 4 | x))
    return out


def split_top(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        depth += (ch == "(") - (ch == ")")
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    return out + [cur]


def _dreg(name):
    m = re.fullmatch(r"[RF](\d+)", name)
    return int(m.group(1)) if m and int(m.group(1)) < 16 else None


def _idx(name, letter):
    m = re.fullmatch(letter + r"(\d+)", name)
    return int(m.group(1)) if m and int(m.group(1)) < 16 else None


class Mem:
    """One DM()/PM() access clause."""

    def __init__(self, space, args, reg, store, lw):
        self.g = space == "PM"
        self.reg, self.store, self.lw = reg, store, lw
        a = split_top(args)
        self.kind, self.i, self.m, self.imm = None, None, None, None
        if len(a) == 1:
            self.kind, self.imm = "abs", a[0]
        elif len(a) == 2:
            i0, m1, m0, i1 = _idx(a[0], "I"), _idx(a[1], "M"), _idx(a[0], "M"), _idx(a[1], "I")
            if i0 is not None and m1 is not None:
                self.kind, self.i, self.m = "post", i0, m1
            elif m0 is not None and i1 is not None:
                self.kind, self.i, self.m = "pre", i1, m0
            elif i0 is not None:
                self.kind, self.i, self.imm = "postimm", i0, a[1]
            elif i1 is not None:
                self.kind, self.i, self.imm = "preimm", i1, a[0]
        if self.kind is None:
            raise AsmError(f"bad address ({args})")
        for r in (self.i, self.m):
            if r is not None and (r >= 8) != self.g:
                raise AsmError(f"{space}() needs DAG{2 if self.g else 1} registers in ({args})")
        self.i3 = None if self.i is None else self.i & 7
        self.m3 = None if self.m is None else self.m & 7


_MEM_LOAD = re.compile(r"^(\w+)=(DM|PM)\((.*)\)(\(LW\))?$")
_MEM_STORE = re.compile(r"^(DM|PM)\((.*)\)=([^()]+?)(\(LW\))?$")
_MODIFY = re.compile(r"^(?:I(\d+)=)?(MODIFY|BITREV)\(I(\d+),(.+)\)$")
_SWAP = re.compile(r"^[RF](\d+)<->S(\d+)$")
_MODS = re.compile(r"\((DB|LA|CI|LR)(?:,(DB|LA|CI|LR))*\)")


def _take_mods(s):
    mods = set()
    while True:
        m = _MODS.search(s)
        if not m:
            return s, mods
        mods |= set(m.group(0)[1:-1].split(","))
        s = s[:m.start()] + s[m.end():]


def _cond(text):
    """Strip 'IF cond' from canonical text -> (text, code or None)."""
    m = re.search(r"\bIF ((?:NOT )?\w+) ?", text)
    if not m:
        return text, None
    if m.group(1) not in COND:
        raise AsmError(f"unknown condition '{m.group(1)}'")
    return (text[:m.start()] + text[m.end():]).strip(), COND.index(m.group(1))


def encode(text, ctx=None):
    """Every valid 48-bit encoding of one instruction, preferred first."""
    ctx = ctx or Ctx()
    t = canon(text.rstrip(";"))
    t = re.sub(r"^_", "", t)
    t = re.sub(r"^(MOVE|COMP|PAR|SHIFT|SWAP) (?!=)", "", t)
    if re.match(r"^(MODIFY|BITREV) .*(MODIFY|BITREV)\(", t):
        t = t.split(" ", 1)[1]
    t = re.sub(r"^DO (?=LCNTR=)", "", t)
    t, cc = _cond(t)
    cond = 31 if cc is None else cc
    plain = cc is None

    def need_plain(what):
        if not plain:
            raise AsmError(f"{what} cannot be conditional")

    # ---- fixed forms
    if t in ("NOP", "IDLE", "EMUIDLE", "RFRAME"):
        need_plain(t)
        return [{"NOP": 0, "IDLE": 1 << 39, "EMUIDLE": 1 << 38, "RFRAME": 0x1900 << 32}[t]]
    parts = split_top(t)
    if all(p in STACKOP for p in parts):
        need_plain("stack operation")
        bits = 0
        for p in parts:
            bits |= 1 << (6 - STACKOP.index(p))
        return [0x17 << 40 | bits << 33]
    m = re.match(r"^BIT (\w+) (\w+) (.+)$", t)
    if m:
        need_plain("BIT")
        if m.group(1) not in BOP or m.group(2) not in _SREG7:
            raise AsmError("BIT needs SET/CLR/TGL/TST/XOR and a system register")
        return [0x14 << 40 | BOP[m.group(1)] << 37 | _SREG7.index(m.group(2)) << 32 | ctx.val(m.group(3)) & 0xFFFFFFFF]

    # ---- loops
    m = re.match(r"^LCNTR=(.+),DO\(?([^()]+)\)?UNTIL LCE$", t)
    if m:
        need_plain("DO")
        rel = (ctx.val(m.group(2)) - ctx.pc) & 0xFFFFFF
        if m.group(1) in UREG_CODE:
            return [0x0D << 40 | UREG_CODE[m.group(1)] << 32 | rel]
        n = ctx.val(m.group(1))
        if not 0 <= n < 0x10000:
            raise AsmError("loop count must fit 16 bits")
        return [0x0C << 40 | n << 24 | rel]
    m = re.match(r"^DO\(?([^()]+)\)?UNTIL ((?:NOT )?\w+)$", t)
    if m:
        need_plain("DO")
        if m.group(2) not in TERM:
            raise AsmError(f"unknown termination '{m.group(2)}'")
        return [0x0E << 40 | TERM.index(m.group(2)) << 33 | (ctx.val(m.group(1)) - ctx.pc) & 0xFFFFFF]

    # ---- program flow
    head = parts[0]
    m = re.match(r"^(JUMP|CALL|CJUMP|RTS|RTI)\b ?(.*)$", head)
    if m:
        kind, rest = m.group(1), m.group(2)
        rest, mods = _take_mods(rest)
        tail = parts[1:]
        els = bool(tail) and tail[0].startswith("ELSE ")
        if els:
            tail[0] = tail[0][5:]
        cmp_ = [0]
        if tail:
            cmp_ = compute_fields(",".join(tail))
            if not cmp_:
                raise AsmError(f"not a compute operation: {','.join(tail)}")
        j, la, ci, lr = ("DB" in mods) << 26, ("LA" in mods) << 38, ("CI" in mods) << 24, ("LR" in mods) << 24
        if kind in ("RTS", "RTI"):
            if rest.strip():
                raise AsmError(f"unexpected '{rest}'")
            return [(0x0A + (kind == "RTI")) << 40 | cond << 33 | j | els << 25 | lr | c for c in cmp_]
        b = (kind == "CALL") << 39
        rest = rest.strip()
        if rest.startswith("(") and rest.endswith(")"):
            rest = rest[1:-1]
        a = split_top(rest)
        if kind == "CJUMP":
            need_plain("CJUMP")
            if a[0] == "PC":
                return [0x1844 << 32 | (ctx.val(a[1]) - ctx.pc) & 0xFFFFFF]
            return [0x1804 << 32 | ctx.val(a[0]) & 0xFFFFFF]
        if len(a) == 2 and _idx(a[0], "M") is not None and _idx(a[1], "I") is not None:
            mi, ii = _idx(a[0], "M"), _idx(a[1], "I")
            if mi < 8 or ii < 8:
                raise AsmError("indirect branches use I8-I15 and M8-M15")
            return [4 << 41 | b | la | cond << 33 | (ii & 7) << 30 | (mi & 7) << 27 | j | els << 25 | ci | c for c in cmp_]
        pcrel = len(a) == 2 and a[0] == "PC"
        if len(a) != (2 if pcrel else 1):
            raise AsmError(f"bad branch target '{rest}'")
        tgt = ctx.val(a[-1])
        out = []
        if not tail and not pcrel:
            out.append(3 << 41 | b | la | cond << 33 | j | ci | tgt & 0xFFFFFF)
        if not tail:
            out.append(3 << 41 | 1 << 40 | b | la | cond << 33 | j | ci | (tgt - ctx.pc) & 0xFFFFFF)
        d = tgt - ctx.pc
        if -32 <= d < 32 or not ctx.final:
            out += [4 << 41 | 1 << 40 | b | la | cond << 33 | (d & 0x3F) << 27 | j | els << 25 | ci | c for c in cmp_]
        if not out:
            raise AsmError("a branch with a compute reaches only PC-32..PC+31")
        return out

    # ---- moves, with or without a compute
    mems, others, comp = [], [], []
    for p in parts:
        m = _MEM_LOAD.match(p)
        if m:
            mems.append(Mem(m.group(2), m.group(3), m.group(1), False, bool(m.group(4))))
            continue
        m = _MEM_STORE.match(p)
        if m:
            mems.append(Mem(m.group(1), m.group(2), m.group(3), True, bool(m.group(4))))
            continue
        m = _MODIFY.match(p)
        if m:
            others.append(("mod", m))
            continue
        m = _SWAP.match(p)
        if m:
            others.append(("swap", m))
            continue
        m = re.match(r"^(\w+)=(\w+)$", p)
        if m and m.group(1) in UREG_CODE and m.group(2) in UREG_CODE:
            others.append(("ureg", m))
            continue
        comp.append(p)
    ctext = ",".join(comp)
    cmp_, shim, imm = [0], None, None
    if comp:
        cmp_ = compute_fields(ctext)
        if not cmp_:
            shim = shimm_fields(ctext, ctx)
            m = re.match(r"^(\w+)=(.+)$", ctext)
            if not shim and len(comp) == 1 and m and m.group(1) in UREG_CODE:
                imm = m
            elif not shim:
                raise AsmError(f"cannot parse '{ctext}'")
    if len(mems) + len(others) > (2 if len(mems) == 2 else 1):
        raise AsmError("too many moves in one instruction")

    if imm is not None:
        if mems or others:
            raise AsmError("an immediate load takes a whole instruction")
        need_plain("an immediate load")
        return [0x0F << 40 | UREG_CODE[imm.group(1)] << 32 | ctx.val(imm.group(2)) & 0xFFFFFFFF]

    if shim is not None:
        if others or len(mems) > 1:
            raise AsmError("an immediate shift takes at most one memory move")
        if not mems:
            return [0x02 << 40 | cond << 33 | ex << 27 | f for ex, f in shim]
        a = mems[0]
        r = _dreg(a.reg)
        if a.kind != "post" or r is None:
            raise AsmError("an immediate shift moves a data register through (Ia, Mb)")
        return [0x8 << 44 | a.i3 << 41 | a.m3 << 38 | cond << 33 | a.g << 32 | a.store << 31 | ex << 27 | r << 23 | f
                for ex, f in shim]

    if len(mems) == 2:
        need_plain("a dual move")
        d = [a for a in mems if not a.g]
        p = [a for a in mems if a.g]
        if len(d) != 1 or len(p) != 1 or d[0].kind != "post" or p[0].kind != "post":
            raise AsmError("a dual move is DM(Ia,Mb) with PM(Ic,Md)")
        d, p = d[0], p[0]
        rd, rp = _dreg(d.reg), _dreg(p.reg)
        if rd is None or rp is None or d.lw or p.lw:
            raise AsmError("a dual move takes data registers")
        return [1 << 45 | d.store << 44 | d.i3 << 41 | d.m3 << 38 | p.store << 37 | rd << 33 |
                p.i3 << 30 | p.m3 << 27 | rp << 23 | c for c in cmp_]

    if mems:
        a = mems[0]
        if a.store and a.reg not in UREG_CODE:                    # type 16: store a constant
            need_plain("a constant store")
            if a.kind != "post" or comp:
                raise AsmError("a constant is stored through (Ia, Mb) with no compute")
            return [0x9 << 44 | a.i3 << 41 | a.m3 << 38 | a.g << 37 | ctx.val(a.reg) & 0xFFFFFFFF]
        if a.reg not in UREG_CODE:
            raise AsmError(f"unknown register '{a.reg}'")
        u = UREG_CODE[a.reg]
        # An I, M, L or B register cannot be stored through its own address
        # generator, nor loaded through it with post-modify: the stock program
        # never does either, and on the chip `I0 = DM(I4, M1)` left I0 unchanged.
        if a.kind != "abs" and 0x10 <= u < 0x50 and (u & 15 >= 8) == bool(a.g) and (a.store or a.kind.startswith("post")):
            raise AsmError(f"{a.reg} cannot be moved this way through an index register of its own address generator")
        if a.kind in ("post", "pre"):
            return [2 << 45 | (a.kind == "post") << 44 | a.i3 << 41 | a.m3 << 38 | cond << 33 | a.g << 32 |
                    a.store << 31 | a.lw << 30 | u << 23 | c for c in cmp_]
        v = ctx.val(a.imm)
        out = []
        if a.kind != "abs" and _dreg(a.reg) is not None and not a.lw:
            try:
                d6 = _signed(v, 6, "offset")
                out += [0x6 << 44 | a.i3 << 41 | a.g << 40 | a.store << 39 | (a.kind == "postimm") << 38 |
                        cond << 33 | d6 << 27 | _dreg(a.reg) << 23 | c for c in cmp_]
            except AsmError:
                if a.kind == "postimm" or comp or not plain:
                    raise
        if not comp and plain and a.kind == "preimm":      # the form ADI's assembler picks
            out.insert(0, 5 << 45 | a.g << 44 | a.i3 << 41 | a.store << 40 | a.lw << 39 | u << 32 | v & 0xFFFFFFFF)
        if not comp and plain and a.kind == "abs":
            out.append(0x04 << 42 | a.g << 41 | a.store << 40 | a.lw << 39 | u << 32 | v & 0xFFFFFFFF)
        if not out:
            raise AsmError("no encoding: a 32-bit offset or address allows no compute, condition or post-modify")
        return out

    if others:
        kind, m = others[0]
        if kind == "ureg":
            s, d = UREG_CODE[m.group(2)], UREG_CODE[m.group(1)]
            return [0xE << 43 | (s >> 2) << 38 | cond << 33 | (s & 3) << 31 | d << 23 | c for c in cmp_]
        if kind == "swap":
            return [0xF << 43 | int(m.group(2)) << 38 | cond << 33 | int(m.group(1)) << 23 | c for c in cmp_]
        src = int(m.group(3))
        dst = src if m.group(1) is None else int(m.group(1))
        if (src ^ dst) & 8:
            raise AsmError("MODIFY source and destination must be in the same DAG")
        mm = _idx(m.group(4), "M")
        if mm is not None and m.group(2) == "MODIFY":
            if (mm ^ src) & 8:
                raise AsmError("MODIFY needs I and M registers of the same DAG")
            return [0x04 << 40 | (src >> 3) << 38 | cond << 33 | (src & 7) << 30 | (mm & 7) << 27 |
                    ((src ^ dst) & 7) << 24 | c for c in cmp_]
        if comp or not plain:
            raise AsmError("MODIFY/BITREV with a constant allows no compute or condition")
        return [0x16 << 40 | (m.group(2) == "BITREV") << 39 | (src >> 3) << 38 | ((src ^ dst) & 7) << 35 |
                (src & 7) << 32 | ctx.val(m.group(4)) & 0xFFFFFFFF]

    return [0x01 << 40 | cond << 33 | c for c in cmp_]


# ------------------------------------------------------------------ source files
class Section:
    def __init__(self, kind, addr):
        self.kind, self.addr, self.words = kind, addr, []


def assemble(src, name="<src>", syms=None):
    """Assemble source text -> (sections, symbols).  pm words are 48-bit ints."""
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    stmts = []
    for no, line in enumerate(src.split("\n"), 1):
        line = line.split("//")[0]
        while True:
            m = re.match(r"\s*([A-Za-z_.]\w*):", line)
            if not m:
                break
            stmts.append((no, "label", m.group(1).upper()))
            line = line[m.end():]
        for s in line.split(";"):
            if s.strip():
                stmts.append((no, "stmt", s.strip()))
    syms = {k.upper(): v for k, v in (syms or {}).items()}
    for final in (False, True):
        sections, sec = [], None
        for no, kind, s in stmts:
            try:
                if kind == "label":
                    if sec is None:
                        raise AsmError("label before any .section")
                    at = sec.addr + len(sec.words)
                    if final and syms.get(s) != at:
                        raise AsmError(f"label '{s}' moved or is defined twice")
                    if not final and s in syms:
                        raise AsmError(f"'{s}' is defined twice")
                    syms[s] = at
                    continue
                ctx = Ctx(0 if sec is None else sec.addr + len(sec.words), syms, final)
                m = re.match(r"\.(\w+)\s*(.*)$", s)
                if not m:
                    if sec is None or sec.kind != "pm":
                        raise AsmError("instruction outside a pm section")
                    sec.words.append(encode(s, ctx)[0])
                    continue
                d, arg = m.group(1).lower(), m.group(2)
                if d == "section":
                    k, a = arg.split()
                    if k not in ("pm", "dm"):
                        raise AsmError(".section pm|dm <address>")
                    sec = Section(k, Ctx(0, syms, True).val(a.upper()))
                    sections.append(sec)
                elif d == "equ":
                    k, a = arg.split(",", 1)
                    syms[k.strip().upper()] = Ctx(0, syms, True).val(a.upper())
                elif sec is None:
                    raise AsmError("data before any .section")
                elif d == "word":
                    mask = (1 << 48) - 1 if sec.kind == "pm" else 0xFFFFFFFF
                    sec.words += [ctx.val(x.upper()) & mask for x in split_top(arg)]
                elif d == "float":
                    sec.words += [struct.unpack("<I", struct.pack("<f", float(x)))[0] for x in arg.split(",")]
                elif d == "space":
                    sec.words += [0] * Ctx(0, syms, True).val(arg.upper())
                else:
                    raise AsmError(f"unknown directive .{d}")
            except AsmError as e:
                raise AsmError(f"{name}:{no}: {e}\n    {s}") from None
    return sections, syms


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    try:
        sections, syms = assemble(open(sys.argv[1]).read(), sys.argv[1])
    except AsmError as e:
        sys.exit(str(e))
    for s in sections:
        print(f"; {s.kind} {s.addr:#x}  {len(s.words)} words")
        for i, w in enumerate(s.words):
            print(f"{s.addr + i:06x}  {w:012x}" if s.kind == "pm" else f"{s.addr + i:06x}  {w:08x}")
    for k, v in sorted(syms.items(), key=lambda kv: kv[1]):
        print(f"; {v:#08x} {k}")


if __name__ == "__main__":
    main()
