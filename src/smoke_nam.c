/* smoke_nam.c - time the NAM kernel on the SHARC and show the result.
 *
 * Boots build/sharc_nam_bench.h (src/sharc/nam_a2_simd.asm + nam_bench.asm
 * with an 8-channel model) exactly as smoke_sharc.c boots hello. The SHARC
 * runs NAM_BENCH_SAMPLES samples with its core timer running, checks the
 * output against what the simulator computed, and then repeats a four-word
 * report on its flag line: magic, core cycles, largest output error (float),
 * last output. Line code (nam_bench.asm): 8 units high + 1 low starts a
 * frame; a bit is 3 units high (one) or 1 unit high (zero), then 1 unit low.
 * Only the ratio of each high time to the low time after it is used.
 *
 * The four white bands of the download come first, as in smoke_sharc.c. Then
 * the screen turns to hex text (smoke_text.h):
 *   row 0      edges seen so far (green); times the line stood still for
 *              LIMIT polls (red); cycles per sample as a percentage of a
 *              48 kHz period at 450 MHz (9,375 cycles), where the simulator
 *              says 56 (86 decimal); frames read; polls of the line during
 *              a frame start (yellow), 8 units of 262,144 core cycles: B2B8
 *              before the SHARC's PLL was set up, less in proportion after
 *   rows 1-9   the words of the last frame read, two to a row (list in
 *              nam_bench.asm). The first, the magic, is green when two
 *              frames in a row agree and the output error is below 1e-4,
 *              red when the error is above, yellow otherwise.
 * Every frame also goes to the computer over USB as a line of hex words
 * (src/usb_mon.h, tools/usbmon.py); the command D from there sends the image
 * back to the DFU loader. */
#define SMOKE_SHARC_NO_MAIN
#include "smoke_sharc.c"
#include "../build/sharc_nam_bench.h"
#include "smoke_text.h"

#define ERR_MAX 0x38D1B717u                   /* 1e-4f */
#define PERIOD 9375u                          /* 450 MHz / 48 kHz */
#define WORDS NAM_BENCH_WORDS
#include "dfu.h"
#include "usb_mon.h"
static void serve(void) {
    uint32_t c;
    usb_poll();
    if (usb_command(&c) && (c & 0xFF) == 'D') dfu_reboot();
}
#define SPAN_POLL() serve()
#include "smoke_frames.h"

int main(void) {
    lcd_start();
    bands(0, 16, 0);
    link_init();
    sharc_boot(sharc_nam_bench, sizeof sharc_nam_bench / sizeof sharc_nam_bench[0]);

    text_start();
    counts();
    usb_start();

    static uint32_t prev[WORDS], w[WORDS];
    for (unsigned frames = 0;;) {
        int got = frame_in(w, WORDS);
        counts();
        if (!got) continue;
        ++frames;
        usb_puts("bench");
        for (unsigned i = 0; i < WORDS; ++i) { usb_putc(' '); usb_puthex(w[i], 8); }
        usb_putc('\n');
        int same = 1;
        for (unsigned i = 0; i < WORDS; ++i) {
            same &= w[i] == prev[i];
            prev[i] = w[i];
        }
        for (unsigned i = 0; i < WORDS; ++i)
            text_hex(i % 2 * 10, 1 + i / 2, w[i], 8,
                     i ? 1 : !same || w[0] != NAM_BENCH_MAGIC ? 4 : w[2] < ERR_MAX ? 2 : 3);
        text_hex(10, 0, w[1] / NAM_BENCH_SAMPLES * 100u / PERIOD, 2, 1);
        text_hex(13, 0, frames, 2, 1);
        text_hex(16, 0, start_polls, 4, 4);
    }
}
