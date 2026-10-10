/* smoke_frames.h - read the frames a SHARC test program sends through
 * src/sharc/report.asm on the line the LPC4350 sees on GPIO7[0]. Include after
 * smoke_sharc.c and smoke_text.h.
 *
 * Line code: 8 units high + 1 low starts a frame; a bit, most significant
 * first, is 3 units high (one) or 1 unit high (zero), then 1 unit low. Only
 * the ratio of each high time to the low time after it is used. */
#define LIMIT 20000000u                       /* polls before a level counts as stuck */

static unsigned level(void) { return GPIO_PIN(DSP_PORT) >> DSP_LINE & 1u; }

/* Polls for as long as the line stays at lv; 0 if it never leaves. */
static unsigned edges, stuck;
static uint32_t start_polls;                  /* length of the last frame start: 8 units */

static uint32_t span(unsigned lv) {
    uint32_t n = 1;
    while (level() == lv)
        if (++n > LIMIT) { ++stuck; return 0; }
    ++edges;
    return n;
}

/* Row 0: edges seen (green), times the line stood still for LIMIT polls (red). */
__attribute__((unused)) static void counts(void) {
    text_hex(0, 0, edges, 6, 2);
    text_hex(7, 0, stuck, 2, 3);
}

/* One frame of n words into w. 0: the line is stuck. */
static int frame_in(uint32_t *w, unsigned n) {
    uint32_t h, l;
    if (!span(1) || !span(0)) return 0;       /* to a rising edge */
    do {
        h = span(1); l = span(0);
        if (!h || !l) return 0;
    } while (h <= 5 * l);                     /* the 8:1 frame start */
    start_polls = h;
    for (unsigned i = 0; i < n; ++i) {
        w[i] = 0;
        for (unsigned b = 0; b < 32; ++b) {
            h = span(1); l = span(0);
            if (!h || !l) return 0;
            w[i] = w[i] << 1 | (h > 2 * l);
        }
    }
    return 1;
}
