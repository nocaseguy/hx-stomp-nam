#!/usr/bin/env python3
"""check_width.py - the UI's text must fit the screen (src/ui/draw.h): a body
line is 39 characters from the left edge, a footer box label 12, a line of
the help 39. Looks at the string literals in src/ui/*.h; exits 1 if one is
too long.  Run from the repository's top: python3 tools/check_width.py"""
import glob
import re
import sys

COLS, KEY_COLS = 39, 12
LIT = r'"((?:[^"\\]|\\.)*)"'


def width(fmt):
    """Characters a format is likely to take: a directive's own width, or a guess."""
    def w(m):
        digits = re.match(r'%[-+0]*(\d+)', m.group(0))
        if digits:
            return 'X' * int(digits.group(1))
        return {'s': 'X' * 8, 't': 'X' * 5, '%': '%'}.get(m.group(0)[-1], 'XXX')
    return len(re.sub(r'%[-+0-9]*[a-zA-Z%]', w, fmt))


bad = 0
for f in sorted(glob.glob('src/ui/*.h')):
    in_help = False
    for n, line in enumerate(open(f), 1):
        if 'help[] = {' in line:
            in_help = True
        elif in_help and line.startswith('}'):
            in_help = False
        elif in_help:
            for lit in re.findall(LIT, line):
                if len(lit.lstrip('#')) > COLS:
                    print('%s:%d: help line over %d: "%s"' % (f, n, COLS, lit)); bad += 1
        for m in re.finditer(r'draw_keys\(([^;]*)', line):
            for lit in re.findall(LIT, m.group(1)):
                if len(lit) > KEY_COLS:
                    print('%s:%d: box label over %d: "%s"' % (f, n, KEY_COLS, lit)); bad += 1
        for m in re.finditer(r'\bfb_text\(\s*(\d+)\s*,([^;]*)', line):
            lits = re.findall(LIT, m.group(2))
            if not lits or lits[0] == '%s':
                continue
            cols = int(m.group(1)) // 8 + width(lits[0])
            if cols > COLS:
                print('%s:%d: %d columns (limit %d): "%s"' % (f, n, cols, COLS, lits[0])); bad += 1
sys.exit(1 if bad else 0)
