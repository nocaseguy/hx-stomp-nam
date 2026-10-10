/* smoke_words.c - boot a SHARC test program that reports through
 * src/sharc/report.asm and pass its frames on: to the screen as hex words,
 * two to a row, and to the computer over USB as a line of text
 * (src/usb_mon.h, tools/usbmon.py; the command D from there sends the image
 * back to the DFU loader).
 *
 * Which program is decided when this is compiled: IMG_H is the header
 * tools/sharc/mkreport.py wrote, IMG its array and IMG_WORDS the length of a
 * frame. `make smoke-stall` builds it with src/sharc/stall_bench.asm. */
#define SMOKE_SHARC_NO_MAIN
#include "smoke_sharc.c"
#include IMG_H
#include "smoke_text.h"
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
    sharc_boot(IMG, sizeof IMG / sizeof IMG[0]);
    text_start();
    usb_start();
    static uint32_t w[IMG_WORDS];
    for (;;) {
        if (!frame_in(w, IMG_WORDS)) continue;
        usb_puts("words");
        for (unsigned i = 0; i < IMG_WORDS; ++i) {
            usb_putc(' ');
            usb_puthex(w[i], 8);
            if (i < 18) text_hex(i % 2 * 10, 1 + i / 2, w[i], 8, i ? 1 : 2);
        }
        usb_putc('\n');
    }
}
