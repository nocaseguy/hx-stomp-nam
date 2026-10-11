/* draw.h - the design system: three zones, one colour language.
 *   HEADER  rows 0..45    what this is: a title and one line under it
 *   BODY    rows 48..219  the page
 *   FOOTER  rows 220..239 three boxes, one over each knob under the screen,
 *                         each saying what pushing that knob does here
 * It is the Sonulab pedal's (its ui/draw.c and DESIGN.md), on a screen 70 rows
 * taller and over three knobs in place of five. A body line is 39 characters;
 * tools/check_width.py holds the text to that. Include after fb.h. */
#ifndef UI_DRAW_H
#define UI_DRAW_H

/* Neon on deep violet. Every colour has one job. */
#define C_BG     RGB565(16, 6, 34)      /* the ground */
#define C_PANEL  RGB565(30, 12, 58)     /* footer boxes, the notice strip */
#define C_FG     RGB565(240, 232, 255)  /* text */
#define C_DIM    RGB565(150, 125, 195)  /* second-rank text, what is off */
#define C_MAG    RGB565(255, 45, 170)   /* neon rose: what is live or chosen */
#define C_SEL    RGB565(120, 16, 90)    /* the chosen row or block */
#define C_BOX    RGB565(90, 10, 70)     /* the footer box that goes ahead */
#define C_BLINK  RGB565(150, 20, 110)   /* ... while it blinks */
#define C_CYAN   RGB565(70, 235, 255)   /* signal cyan: what the hand can do */
#define C_OK     RGB565(60, 230, 120)
#define C_WARN   RGB565(255, 210, 60)
#define C_ERR    RGB565(255, 70, 110)
#define C_NONE   FB_TRANSPARENT
/* The blocks' own colours, in footswitch order, on the screen and on the rings
 * (red, green, blue, 0-100, for the ring message). Used for nothing else. */
static const uint16_t block_col[3] = { RGB565(60, 230, 90), RGB565(255, 170, 30), RGB565(255, 70, 60) };
static const uint8_t block_ring[3][3] = { {0, 100, 10}, {100, 45, 0}, {100, 0, 0} };

#define HEADER_H    46
#define STAGE_HDR_H 28
#define BODY_Y      48
#define FOOTER_Y    220
#define ROW         12
#define COLS        39
#define KEYS        3
#define KEY_COLS    12

/* Once a frame: the time in milliseconds, for what scrolls and blinks, and
 * which of the three knobs are held down (bit i = box i). */
static uint32_t draw_now, draw_held;
static void draw_begin(uint32_t now, uint32_t held) { draw_now = now; draw_held = held; }

static int draw_len(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static int key_x(int i) { return i * FB_W / KEYS; }

/* ---- text that may not fit its box: it runs back and forth, a pause at each
 * end, so all of it can be read ---- */
static int fit_offset(int over, int y) {
    const int hold = 1200, run = over * 1000 / 24;          /* 24 pixels a second */
    int ph = (int)((draw_now + (uint32_t)y * 230u) % (uint32_t)(2 * (hold + run)));   /* rows out of step */
    if (ph < hold) return 0;
    if (ph < hold + run) return (ph - hold) * 24 / 1000;
    if (ph < 2 * hold + run) return over;
    return over - (ph - 2 * hold - run) * 24 / 1000;
}
static void draw_fit(int x, int y, int w, uint16_t fg, const char *s) {
    int over = draw_len(s) * FONT_W - w;
    if (over <= 0) { fb_text(x, y, fg, C_NONE, "%s", s); return; }
    fb_clip_x(x, x + w);
    fb_text(x - fit_offset(over, y), y, fg, C_NONE, "%s", s);
    fb_clip_x(0, FB_W);
}
static void draw_fit_disp(int x, int y, int w, uint16_t fg, const char *s) {
    int over = fb_disp_width(DISP_TITLE, s) - w;
    if (over <= 0) { fb_text_disp(x, y, DISP_TITLE, fg, "%s", s); return; }
    fb_clip_x(x, x + w);
    fb_text_disp(x - fit_offset(over, y), y, DISP_TITLE, fg, "%s", s);
    fb_clip_x(0, FB_W);
}
/* How long a message of this length needs to be read once, milliseconds. */
static uint32_t draw_fit_time(const char *s) {
    int over = (draw_len(s) - COLS) * FONT_W;
    return over > 0 ? 5200u + (uint32_t)over * 1000u / 24u : 4000u;
}

/* ---- HEADER of the pages that cover the stage ---- */
static void draw_page(const char *title, const char *sub) {
    fb_rect(0, 0, FB_W, FB_H, C_BG);
    fb_rect(0, HEADER_H, FB_W, 2, C_MAG);
    draw_fit_disp(6, 5, FB_W - 10, C_FG, title);
    if (sub && sub[0]) draw_fit(6, 31, FB_W - 10, C_DIM, sub);
}

/* ---- FOOTER: three boxes in the order of the knobs under them. Empty: that
 * push does nothing here. `primary` (-1: none) is the one that goes ahead,
 * and may blink. A box is lit for as long as its knob is held down. ---- */
static void draw_keys(const char *k1, const char *k2, const char *k3, int primary, int blink) {
    const char *lab[KEYS] = { k1, k2, k3 };
    fb_shade(0, FOOTER_Y, FB_W, FB_H - FOOTER_Y, C_PANEL);
    fb_rect(0, FOOTER_Y, FB_W, 2, C_CYAN);
    for (int i = 0; i < KEYS; ++i) {
        int x = key_x(i), w = key_x(i + 1) - x, down = (int)(draw_held >> i & 1u), n = draw_len(lab[i]);
        if (i) fb_rect(x, FOOTER_Y + 2, 2, FB_H - FOOTER_Y - 2, C_DIM);
        if (down) fb_rect(x + 2, FOOTER_Y + 3, w - 4, FB_H - FOOTER_Y - 4, i == primary ? C_MAG : C_CYAN);
        else if (i == primary) fb_rect(x + 2, FOOTER_Y + 3, w - 4, FB_H - FOOTER_Y - 4, blink ? C_BLINK : C_BOX);
        if (n > KEY_COLS) n = KEY_COLS;
        if (!n) continue;
        if (down) { fb_clip_x(x, x + w); fb_glyphs(lab[i], x + (w - FONT_W * n) / 2, FOOTER_Y + 5, C_BG); fb_clip_x(0, FB_W); }
        else fb_text(x + (w - FONT_W * n) / 2, FOOTER_Y + 5, i == primary ? C_FG : C_CYAN, C_NONE, "%s", lab[i]);
    }
}

/* ---- NOTICE: a strip of news (green) or of a problem (gold) at y ---- */
static void draw_notice(int y, const char *msg, int warn, int over_scene) {
    uint16_t c = warn ? C_WARN : C_OK;
    if (over_scene) fb_shade(0, y, FB_W, 14, C_BG);
    else { fb_rect(0, y, FB_W, 14, C_PANEL); fb_rect(0, y, FB_W, 1, c); }
    draw_fit(4, y + 2, FB_W - 8, c, msg);
}

/* ---- a value over its knob: a slot shaped like the footer box under it,
 * the text centred, a two-pixel bar that reads as a fader from a metre away.
 * The bar runs from the middle when the value can go below nought, from the
 * left when it cannot. `lit`: the one the hand has just turned. ---- */
static void draw_cell(int i, int y, const char *text, int v, int min, int max, int lit) {
    int x = key_x(i), w = key_x(i + 1) - x, n = draw_len(text);
    uint16_t c = lit ? C_CYAN : C_FG;
    fb_shade(x, y, w, 18, C_PANEL);
    if (i) fb_rect(x, y, 2, 18, C_DIM);
    if (!text[0]) return;
    fb_text(x + (w - FONT_W * n) / 2, y + 1, c, C_NONE, "%s", text);
    int bx = x + 8, bw = w - 16;
    fb_rect(bx, y + 15, bw, 1, C_DIM);
    if (min < 0) {
        int cx = bx + bw / 2, span = v < 0 ? -min : max, len = span ? (bw / 2) * (v < 0 ? -v : v) / span : 0;
        fb_rect(cx, y + 14, 1, 3, C_FG);
        if (v > 0) fb_rect(cx + 1, y + 14, len, 3, c);
        else if (v < 0) fb_rect(cx - len, y + 14, len, 3, c);
    } else {
        fb_rect(bx, y + 14, 1, 3, C_FG);
        fb_rect(bx + 1, y + 14, max > min ? (bw - 1) * (v - min) / (max - min) : 0, 3, c);
    }
}

/* ---- a list row: the cursor's row on the rose band; the value to the
 * right, cyan when something is set or on, dim when it is off or only a way
 * in. A value too long for its field scrolls. ---- */
static void draw_row(int y, int sel, const char *label, const char *value, int lit) {
    int w = draw_len(value) * FONT_W;
    if (sel) fb_rect(0, y - 1, FB_W, ROW, C_SEL);
    fb_text(12, y, sel ? C_FG : C_DIM, C_NONE, "%s", label);
    if (w <= 104) fb_text(206 + 104 - w, y, lit ? C_CYAN : C_DIM, C_NONE, "%s", value);
    else draw_fit(206, y, 104, lit ? C_CYAN : C_DIM, value);
}

/* The two-pixel bar at the right of a list: `show` of `n` rows from `first`. */
static void draw_scrollbar(int y, int show, int n, int first) {
    if (n <= show) return;
    int zone = ROW * show, th = zone * show / n;
    if (th < 4) th = 4;
    fb_rect(FB_W - 2, y - 1, 2, zone, C_PANEL);
    fb_rect(FB_W - 2, y - 1 + (zone - th) * first / (n - show), 2, th, C_CYAN);
}

#endif
