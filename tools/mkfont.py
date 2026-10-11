#!/usr/bin/env python3
"""mkfont.py - render the UI's two bitmap faces into src/ui/font.h.

  body      Sixtyfour (OFL) at 8 px -> 8 x 12 cells, ASCII 32..126
  headline  Jersey 15 (OFL) at 36 px -> 24 rows, proportional: an advance and
            rows of 32-bit masks (bit 31 = left) per glyph, ASCII 32..126
  number    Jersey 15 at 54 px -> 36 rows, the digits only

    python3 tools/mkfont.py > src/ui/font.h      (needs Pillow; faces in tools/fonts/)
    python3 tools/mkfont.py --show               (a few glyphs as text)

The faces and sizes are those of the Sonulab pedal's UI, which this one follows."""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 8, 12
BODY = (os.path.join(HERE, 'fonts/Sixtyfour.ttf'), 8, 0, 2)         # path, size, x offset, y offset
JERSEY = os.path.join(HERE, 'fonts/Jersey15.ttf')
DISP = [('DISP_A', 36, 24, [chr(c) for c in range(32, 127)]),       # name, size, rows, characters
        ('DISP_B', 54, 36, list('0123456789'))]
ASCII = [chr(c) for c in range(32, 127)]


def cap_offset(font):
    """The y offset that puts the top of 'H' on row 1."""
    img = Image.new('L', (60, 80), 0)
    ImageDraw.Draw(img).text((0, 0), 'H', font=font, fill=255)
    return 1 - img.getbbox()[1]


def body_glyph(ch):
    path, size, xo, yo = BODY
    font = ImageFont.truetype(path, size)
    asc, desc = font.getmetrics()
    base = min(asc, H - desc)
    img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(img).text((xo, base - asc + yo), ch, font=font, fill=255)
    return [sum(0x80 >> x for x in range(W) if img.getpixel((x, y)) > 80) for y in range(H)]


def disp_glyph(font, rows, yo, ch):
    """(advance, row masks); the glyph is cut at 32 px."""
    img = Image.new('L', (40, rows), 0)
    ImageDraw.Draw(img).text((0, yo), ch, font=font, fill=255)
    adv = min(32, int(round(font.getlength(ch))))
    return adv, [sum(0x80000000 >> x for x in range(32) if img.getpixel((x, y)) > 80) for y in range(rows)]


def note(ch):
    return ch if ch not in '\\*' else "'" + ch + "'"


if '--show' in sys.argv:
    for ch in 'Ag0':
        print(repr(ch))
        for r in body_glyph(ch):
            print(''.join('#' if r & (0x80 >> x) else '.' for x in range(W)))
    f = ImageFont.truetype(JERSEY, 36)
    for ch in '1Cg':
        adv, masks = disp_glyph(f, 24, cap_offset(f), ch)
        print(repr(ch), adv)
        for m in masks:
            print(''.join('#' if m & (0x80000000 >> x) else '.' for x in range(adv)))
    sys.exit()

print('/* font.h - made by tools/mkfont.py; do not edit.')
print(' * Body: Sixtyfour, 8 x 12 cells. Headline: Jersey 15, 24 rows, proportional.')
print(' * Number: Jersey 15, 36 rows, digits only. Both faces are under the SIL Open')
print(' * Font License (tools/fonts/OFL-*.txt). */')
print('#define FONT_W %d\n#define FONT_H %d' % (W, H))
print('static const unsigned char FONT[95][%d] = {' % H)
for ch in ASCII:
    print('  {%s}, /* %s */' % (','.join('0x%02x' % r for r in body_glyph(ch)), note(ch)))
print('};')
for name, size, rows, chars in DISP:
    font = ImageFont.truetype(JERSEY, size)
    yo = cap_offset(font)
    glyphs = [disp_glyph(font, rows, yo, ch) for ch in chars]
    print('#define %s_H %d' % (name, rows))
    print('static const unsigned char %s_ADV[%d] = {' % (name, len(chars)))
    print('  ' + ','.join(str(a) for a, _ in glyphs) + ' };')
    print('static const unsigned int %s[%d][%d] = {' % (name, len(chars), rows))
    for ch, (_, masks) in zip(chars, glyphs):
        print('  {%s}, /* %s */' % (','.join('0x%08x' % m for m in masks), note(ch)))
    print('};')
