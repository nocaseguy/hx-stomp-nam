/* fb.h - the screen the UI draws on: 320 x 240, 16 bits a pixel (5:6:5), as
 * the unit is read, (0,0) top left. Drawing goes to a back buffer; whoever
 * includes this shows it (src/stomp.c turns it into the panel's scan order,
 * test/sim/pedal_sim.c writes a picture).
 *
 * The calls and the two faces are the Sonulab pedal's (its hal/fb.c), whose
 * UI this one follows. Text takes a format, see fb_fmt(). FB_BACK must name
 * the buffer before this is included. */
#ifndef UI_FB_H
#define UI_FB_H
#include <stdarg.h>
#include <stdint.h>
#include "font.h"

#define FB_W 320
#define FB_H 240
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define FB_TRANSPARENT 0x0821           /* as fb_text's ground: no box, a black shadow instead */
#define DISP_TITLE  0                   /* the headline face, 24 rows */
#define DISP_NUMBER 1                   /* the number face, 36 rows, digits only */

static void fb_rect(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    for (int j = 0; j < h; ++j) {
        uint16_t *p = FB_BACK + (y + j) * FB_W + x;
        for (int i = 0; i < w; ++i) p[i] = c;
    }
}

/* A rectangle of c mixed half and half with what is under it. */
__attribute__((unused)) static void fb_shade(int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    c = (uint16_t)((c & 0xF7DE) >> 1);
    for (int j = 0; j < h; ++j) {
        uint16_t *p = FB_BACK + (y + j) * FB_W + x;
        for (int i = 0; i < w; ++i) p[i] = (uint16_t)(((p[i] & 0xF7DE) >> 1) + c);
    }
}

/* Text is cut to the columns [x0, x1): for fields whose text scrolls. */
static int fb_clip_x0 = 0, fb_clip_x1 = FB_W;
__attribute__((unused)) static void fb_clip_x(int x0, int x1) {
    fb_clip_x0 = x0 < 0 ? 0 : x0;
    fb_clip_x1 = x1 > FB_W ? FB_W : x1;
}

/* A format into out (n bytes with the end): %s %c %d %u %x %%, with + (always
 * a sign), 0 (leading zeros), a width, and - (to the left); and %t, a whole
 * number of tenths written with its decimal point (-25 is "-2.5"). Returns
 * the length. */
static int fb_vfmt(char *out, int n, const char *f, va_list ap) {
    int o = 0;
#define FB_PUT(ch) do { if (o < n - 1) out[o] = (char)(ch); ++o; } while (0)
    for (; *f; ++f) {
        if (*f != '%') { FB_PUT(*f); continue; }
        int plus = 0, zero = 0, left = 0, width = 0;
        for (++f; *f == '+' || *f == '0' || *f == '-'; ++f) { plus |= *f == '+'; zero |= *f == '0'; left |= *f == '-'; }
        for (; *f >= '0' && *f <= '9'; ++f) width = width * 10 + (*f - '0');
        char t[16];
        const char *s = t;
        int len = 0;
        if (*f == 's') { s = va_arg(ap, const char *); while (s[len]) ++len; }
        else if (*f == 'c') { t[0] = (char)va_arg(ap, int); len = 1; }
        else if (*f == '%') { t[0] = '%'; len = 1; }
        else if (*f == 'd' || *f == 'u' || *f == 'x' || *f == 't') {
            int v = va_arg(ap, int), neg = *f != 'u' && *f != 'x' && v < 0;
            unsigned u = neg ? 0u - (unsigned)v : (unsigned)v, base = *f == 'x' ? 16 : 10;
            char r[16];
            int k = 0;
            if (*f == 't') { r[k++] = (char)('0' + u % 10); r[k++] = '.'; u /= 10; }
            do { r[k++] = "0123456789ABCDEF"[u % base]; u /= base; } while (u);
            if (zero) while (k < width - (neg || plus) && k < 14) r[k++] = '0';
            if (neg) r[k++] = '-'; else if (plus) r[k++] = '+';
            while (k) t[len++] = r[--k];
        } else { if (!*f) break; continue; }
        if (!left) for (int i = len; i < width; ++i) FB_PUT(' ');
        for (int i = 0; i < len; ++i) FB_PUT(s[i]);
        if (left) for (int i = len; i < width; ++i) FB_PUT(' ');
    }
#undef FB_PUT
    out[o < n ? o : n - 1] = 0;
    return o < n ? o : n - 1;
}
__attribute__((unused)) static int fb_fmt(char *out, int n, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int len = fb_vfmt(out, n, f, ap);
    va_end(ap);
    return len;
}

static void fb_glyphs(const char *s, int x, int y, uint16_t fg) {
    for (; *s; ++s, x += FONT_W) {
        if (x + FONT_W <= fb_clip_x0 || x >= fb_clip_x1) continue;
        unsigned ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char *g = FONT[ch - 32];
        for (int r = 0; r < FONT_H; ++r) {
            int py = y + r;
            unsigned row = g[r];
            if (py < 0 || py >= FB_H || !row) continue;
            for (int c = 0; c < FONT_W; ++c) {
                int px = x + c;
                if (row & (0x80u >> c) && px >= fb_clip_x0 && px < fb_clip_x1) FB_BACK[py * FB_W + px] = fg;
            }
        }
    }
}

/* Body text at (x, y). On FB_TRANSPARENT it gets a one-pixel black shadow, so
 * it reads over whatever moves under it; on any other ground, a box of it. */
static void fb_text(int x, int y, uint16_t fg, uint16_t bg, const char *f, ...) {
    char s[96];
    va_list ap;
    va_start(ap, f);
    int len = fb_vfmt(s, sizeof s, f, ap);
    va_end(ap);
    if (bg == FB_TRANSPARENT) fb_glyphs(s, x + 1, y + 1, 0x0000);
    else fb_rect(x, y, len * FONT_W, FONT_H, bg);
    fb_glyphs(s, x, y, fg);
}

static int fb_disp_glyph(int size, char c) {        /* its place in the face's table, or -1 */
    unsigned ch = (unsigned char)c;
    if (size == DISP_NUMBER) return ch >= '0' && ch <= '9' ? (int)ch - '0' : -1;
    return (int)(ch < 32 || ch > 126 ? '?' : ch) - 32;
}

__attribute__((unused)) static int fb_disp_width(int size, const char *s) {
    int w = 0;
    for (; *s; ++s) {
        int g = fb_disp_glyph(size, *s);
        if (g >= 0) w += size == DISP_NUMBER ? DISP_B_ADV[g] : DISP_A_ADV[g];
    }
    return w;
}

static void fb_disp_glyphs(const char *s, int x, int y, int size, uint16_t fg) {
    int rows = size == DISP_NUMBER ? DISP_B_H : DISP_A_H;
    for (; *s; ++s) {
        int gi = fb_disp_glyph(size, *s);
        if (gi < 0) continue;
        const unsigned int *g = size == DISP_NUMBER ? DISP_B[gi] : DISP_A[gi];
        int w = size == DISP_NUMBER ? DISP_B_ADV[gi] : DISP_A_ADV[gi];
        if (x + w + 2 > fb_clip_x0 && x < fb_clip_x1)
            for (int r = 0; r < rows; ++r) {
                int py = y + r;
                unsigned int row = g[r];
                if (py < 0 || py >= FB_H || !row) continue;
                for (int c = 0; c < w + 2 && c < 32; ++c) {
                    int px = x + c;
                    if (row & (0x80000000u >> c) && px >= fb_clip_x0 && px < fb_clip_x1) FB_BACK[py * FB_W + px] = fg;
                }
            }
        x += w;
    }
}

/* Headline or number face at (x, y), with a two-pixel black shadow. */
__attribute__((unused)) static void fb_text_disp(int x, int y, int size, uint16_t fg, const char *f, ...) {
    char s[64];
    va_list ap;
    va_start(ap, f);
    fb_vfmt(s, sizeof s, f, ap);
    va_end(ap);
    fb_disp_glyphs(s, x + 2, y + 2, size, 0x0000);
    fb_disp_glyphs(s, x, y, size, fg);
}

#endif
