/* smoke_text.h - hex digits on the smoke_lcd.c screen, for results that do not
 * fit in sixteen colour bands. Include after smoke_lcd.c.
 *
 * The panel is scanned 240 x 320 and mounted on its side: its line number runs
 * left to right as the unit is read. Text is laid out for that view, 320 wide
 * and 240 high, in a 3 x 5 font drawn four times its size: 20 columns by 10
 * rows. Characters: 0-9, A-Z (lower case is drawn as upper), - . : / + < > = #;
 * anything else is a space.
 * Palette after text_start(): 0 black, 1 white, 2 green, 3 red, 4 yellow. */
#define TEXT_COLS 20
#define TEXT_ROWS 10

static void text_start(void) {
    static const uint16_t c[5] = { 0x0000, 0x7FFF, 0x03E0, 0x001F, 0x03FF };
    uint8_t *fb = (uint8_t *)SMOKE_FB;
    for (unsigned i = 0; i < SMOKE_W / 2 * SMOKE_H; ++i) fb[i] = 0;
    LCD_PAL(0) = c[0] | (uint32_t)c[1] << 16;
    LCD_PAL(1) = c[2] | (uint32_t)c[3] << 16;
    LCD_PAL(2) = c[4];
    for (unsigned i = 3; i < 8; ++i) LCD_PAL(i) = 0;
}

/* One pixel in the reader's view: x 0..319 left to right, y 0..239 top down. */
static void text_px(unsigned x, unsigned y, unsigned colour) {
    y = SMOKE_W - 1 - y;                        /* the panel's columns run bottom to top */
    uint8_t *p = (uint8_t *)SMOKE_FB + x * (SMOKE_W / 2) + y / 2;
    *p = y & 1 ? (uint8_t)((*p & 0x0F) | colour << 4) : (uint8_t)((*p & 0xF0) | colour);
}

/* 3 x 5 glyphs, five rows of three bits, top row first. */
static uint16_t text_glyph(char ch) {
    static const uint16_t digit[10] = {
        075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717,
    };
    static const uint16_t letter[26] = {
        025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011152,
        055655, 044447, 057755, 065555, 025552, 065644, 025573, 065655, 034216, 072222,
        055557, 055552, 055775, 055255, 055222, 071247,
    };
    if (ch >= '0' && ch <= '9') return digit[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return letter[ch - 'A'];
    if (ch >= 'a' && ch <= 'z') return letter[ch - 'a'];
    switch (ch) {
    case '-': return 000700;
    case '.': return 000002;
    case ':': return 002020;
    case '/': return 011244;
    case '+': return 002720;
    case '>': return 042124;
    case '<': return 012421;
    case '=': return 007070;
    case '#': return 077777;
    default:  return 0;
    }
}

static void text_char(unsigned col, unsigned row, char ch, unsigned colour) {
    uint16_t g = text_glyph(ch);
    for (unsigned r = 0; r < 5; ++r)
        for (unsigned c = 0; c < 3; ++c) {
            unsigned on = g >> (14 - (r * 3 + c)) & 1u;
            for (unsigned i = 0; i < 16; ++i)
                text_px(col * 16 + c * 4 + (i & 3), row * 24 + r * 4 + (i >> 2), on ? colour : 0);
        }
}

/* A string at (col, row), cut at the right edge; returns the column after it. */
__attribute__((unused)) static unsigned text_str(unsigned col, unsigned row, const char *s, unsigned colour) {
    for (; *s && col < TEXT_COLS; ++s) text_char(col++, row, *s, colour);
    return col;
}

/* `digits` hex digits of v at (col, row); returns the column after them. */
static unsigned text_hex(unsigned col, unsigned row, uint32_t v, unsigned digits, unsigned colour) {
    for (unsigned i = 0; i < digits; ++i)
        text_char(col + i, row, "0123456789ABCDEF"[v >> 4 * (digits - 1 - i) & 15u], colour);
    return col + digits;
}
