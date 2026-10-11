/* smoke_sdram.c - proof of life for the SDRAM and the screen in full colour,
 * the two things src/stomp.c needs that no image has yet had.
 *
 * LCD as in smoke_lcd.c (16 colours, internal RAM), where it writes what it
 * finds; the same goes to the computer (src/usb_mon.h, tools/usbmon.py):
 *   sdram: step <1-8>                     where the set-up has got to (see step())
 *   sdram: set up, core MHz <n> emc MHz <n>
 *   sdram: test ok                        or   test FAILED at <address> wrote <w> read <r>
 *   sdram: <n> MB                         where a write first shows up again at address 0
 * Then, if the test passed and after a few seconds to read that, the screen
 * goes to 16 bits a pixel from the SDRAM (screen16.h) and shows eight bars,
 * each with its colour's name on it, over ramps of red, green and blue. A
 * bar whose colour is not its name, or ramps in another order, means the
 * colour order in screen_start() is wrong.
 * It waits for command G from the computer before it starts (or goes on by
 * itself after a while). Command D: back to the DFU loader.
 *
 * Runs on the unit: set-up and test pass, and the bars are the colours they
 * are named (way 7 below, which is what screen_start() now does). */
#define SMOKE_LCD_NO_MAIN
#include "smoke_lcd.c"
#include "smoke_text.h"
#include "dfu.h"
#include "usb_mon.h"
static void wait(uint32_t n);
static void step(unsigned n);
#define SDRAM_STEP(n) step(n)
#include "sdram.h"

#define SCREEN_SCAN0 (SDRAM_BASE + 0x000000u)
#define SCREEN_SCAN1 (SDRAM_BASE + 0x040000u)
#define FB_BACK ((uint16_t *)(SDRAM_BASE + 0x080000u))
#define SCREEN_MEMORY 0x0C0000u
#include "screen16.h"
#include "ui/fb.h"

/* Poll the computer for about `n` spins' worth; D goes to the loader. */
static void wait(uint32_t n) {
    uint32_t c;
    for (uint32_t i = 0; i < n / 64; ++i) {
        usb_poll();
        if (usb_command(&c) && (c & 0xFF) == 'D') dfu_reboot();
        spin(40);
    }
}

/* Where sdram_start() has got to, to the computer and the screen, with time
 * for the computer to read it before the next thing is tried: 1 measuring
 * the clock, 2 measured, 3 EMC off, 4 its clock set, 5 pins set, 6 times set,
 * 7 NOP and precharge done, 8 all done. The last one seen is where it stopped. */
static void step(unsigned n) {
    usb_puts("sdram: step "); usb_puthex(n, 1);
    if (n == 2) { usb_puts(" core MHz "); usb_puthex(sdram_core_mhz, 2); }
    usb_putc('\n');
    text_hex(6 + n, 0, n, 1, 4);
    wait(3000000);
}

/* A fault (a bus error on the memory, say) is told to the computer, with the
 * processor's own account of it, and D still goes back to the loader. The
 * vector table is in RAM with the image (startup.c), so it can be pointed
 * here. */
extern void (*const g_vectors[])(void);
static void fault(void) {
    usb_puts("fault: cfsr "); usb_puthex(REG32(0xE000ED28u), 8);
    usb_puts(" hfsr "); usb_puthex(REG32(0xE000ED2Cu), 8);
    usb_puts(" bfar "); usb_puthex(REG32(0xE000ED38u), 8); usb_putc('\n');
    text_str(0, 6, "FAULT", 3);
    for (;;) wait(1000000);
}
static void faults_here(void) {
    uint32_t *v = (uint32_t *)(uintptr_t)g_vectors;
    for (unsigned i = 2; i < 7; ++i) v[i] = (uint32_t)fault | 1u;
    REG32(0xE000ED08u) = (uint32_t)v;           /* VTOR */
}

/* Megabytes before the memory repeats: a mark at address 0, then a different
 * word at each power of two; the first that changes the mark is the size. */
static unsigned megabytes(void) {
    volatile uint32_t *m = (volatile uint32_t *)SDRAM_BASE;
    for (unsigned mb = 1; mb <= 64; mb *= 2) {           /* 128 MB on would be the next chip select */
        m[0] = 0x5AFE0000u;
        m[mb << 18] = 0xA11A5000u + mb;
        if (m[0] != 0x5AFE0000u) return mb;
    }
    return 0;                                   /* none up to 64 MB: the test does not say */
}

int main(void) {
    lcd_start();
    text_start();
    faults_here();
    usb_start();
    text_str(0, 0, "SDRAM", 1);
    { uint32_t c = 0;                           /* on the bus first, and until the computer says G (or a while) */
      for (uint32_t i = 0; i < 4000000u && (c & 0xFF) != 'G'; ++i) { usb_poll(); if (usb_command(&c) && (c & 0xFF) == 'D') dfu_reboot(); } }
    usb_puts("sdram: start\n");
    int up = sdram_start();
    usb_puts(up ? "sdram: set up, core MHz " : "sdram: the core clock would not be measured\n");
    if (up) { usb_puthex(sdram_core_mhz, 2); usb_puts(" emc MHz "); usb_puthex(sdram_emc_mhz, 2); usb_puts(" (hex)\n"); }
    text_str(0, 1, up ? "SET UP  CORE MHZ" : "NO CLOCK MEASURE", up ? 2 : 3);
    if (up) text_hex(17, 1, sdram_core_mhz, 2, 4);
    if (up) { usb_puts("sdram: testing\n"); wait(3000000); }
    int ok = up && sdram_test(SDRAM_BASE, SCREEN_MEMORY);
    if (up && ok) {
        usb_puts("sdram: test ok\n");
        text_str(0, 2, "TEST OK", 2);
        wait(3000000);
        unsigned mb = megabytes();
        usb_puts("sdram: "); usb_puthex(mb, 2); usb_puts(" MB (hex)\n");
        text_str(0, 3, "MB", 1); text_hex(3, 3, mb, 2, 4);
    } else if (up) {
        usb_puts("sdram: test FAILED at "); usb_puthex(sdram_bad_at, 8);
        usb_puts(" wrote "); usb_puthex(sdram_bad_wrote, 8);
        usb_puts(" read "); usb_puthex(sdram_bad_read, 8); usb_putc('\n');
        text_str(0, 2, "TEST FAILED AT", 3);
        text_hex(0, 3, sdram_bad_at, 8, 4);
        text_hex(0, 4, sdram_bad_wrote, 8, 1); text_hex(10, 4, sdram_bad_read, 8, 3);
    }
    if (!ok) for (;;) wait(1000000);
    wait(60000000);

    static const struct { const char *name; uint16_t c; } bar[8] = {
        { "WHITE", RGB565(255, 255, 255) }, { "YELLOW", RGB565(255, 255, 0) }, { "CYAN", RGB565(0, 255, 255) },
        { "GREEN", RGB565(0, 255, 0) }, { "PINK", RGB565(255, 0, 255) }, { "RED", RGB565(255, 0, 0) },
        { "BLUE", RGB565(0, 0, 255) }, { "BLACK", 0 },
    };
    fb_rect(0, 0, FB_W, FB_H, 0);
    for (int i = 0; i < 8; ++i) {
        fb_rect(i * 40, 0, 40, 130, bar[i].c);
        fb_text(i * 40 + 1, 136, RGB565(255, 255, 255), FB_TRANSPARENT, "%s", bar[i].name);
    }
    for (int x = 0; x < FB_W; ++x) {
        int v = x * 255 / (FB_W - 1);
        fb_rect(x, 156, 1, 24, RGB565(v, 0, 0));
        fb_rect(x, 184, 1, 24, RGB565(0, v, 0));
        fb_rect(x, 212, 1, 24, RGB565(0, 0, v));
    }
    fb_text(4, 162, RGB565(255, 255, 255), FB_TRANSPARENT, "RED");
    fb_text(4, 190, RGB565(255, 255, 255), FB_TRANSPARENT, "GREEN");
    fb_text(4, 218, RGB565(255, 255, 255), FB_TRANSPARENT, "BLUE");
    screen_start();
    screen_flip();
    screen_flip();                              /* the picture in both of the controller's buffers */
    usb_puts("screen: 16 bits a pixel, eight named bars over three ramps\n");

    /* Ways of showing it, for finding the one the panel takes; a digit from
     * the computer picks one, and what the glass then shows is the answer:
     *   0  back to the 16-colour text screen in internal RAM
     *   1  16 bits 5:6:5, red high (what screen_start() does), controller off and on
     *   2  16 bits 5:6:5, red low
     *   3  16 bits 1:5:5:5
     *   4  as 1, the mode changed without turning the controller off
     *   5  the 16-colour text screen, copied to the SDRAM and shown from there
     *   6  24 bits (the buffer is the wrong shape for it: only whether it lights)
     *   7  as 1, and the panel set up again afterwards as lcd_start() does
     *   8  as 2, likewise
     *   9  as 0, likewise */
    for (;;) {
        uint32_t c;
        usb_poll();
        if (!usb_command(&c)) continue;
        unsigned k = (c & 0xFF) - '0';
        if ((c & 0xFF) == 'D') dfu_reboot();
        if (k > 9) continue;
        static const uint8_t bpp[10] = { 2, 6, 6, 4, 6, 2, 5, 6, 6, 2 };
        uint32_t ctrl = (uint32_t)bpp[k] << 1 | 1u << 5 | 1u << 11 | (k == 1 || k == 4 || k == 7 ? 1u << 8 : 0);
        uint32_t base = k == 0 || k == 9 ? SMOKE_FB : k == 5 ? SDRAM_BASE + 0x0A0000u : (uint32_t)screen_scan[0];
        if (k == 5) for (unsigned i = 0; i < SMOKE_W / 2 * SMOKE_H; ++i)
            ((volatile uint8_t *)base)[i] = ((const uint8_t *)SMOKE_FB)[i];
        if (k == 4) { LCD_UPBASE = base; LCD_CTRL = ctrl | 1u; }
        else { LCD_CTRL &= ~1u; spin(3000000); LCD_UPBASE = base; LCD_CTRL = ctrl; LCD_CTRL |= 1u; }
        if (k >= 7) { spin(3000000); lcd_panel_setup(); spin(3000000); lcd_panel_on(); }
        usb_puts("screen: way "); usb_puthex(k, 1); usb_puts(" ctrl "); usb_puthex(LCD_CTRL, 8);
        usb_puts(" base "); usb_puthex(LCD_UPBASE, 8); usb_putc('\n');
    }
}
