/* smoke_lcd.c - stage-1 proof of life: LCD only, no PLL, no SDRAM.
 *
 * The controller values are the stock firmware's own: 240 x 320 (PPL = 240, LPP = 320), TFT,
 * HBP/HFP/HSW = 20/10/10, vertical fields 2/4/2, IPC+IHS+IVS inverted,
 * LcdPwr, wait, LcdEn. Pins, backlight and the panel's own set-up are in
 * lcd_panel.h. Works on the unit.
 *
 * Deliberate differences from stock, so this image depends on nothing else:
 *   - BASE_LCD_CLK comes from the 12 MHz IRC (stock: PLL1), PCD=0 -> /2 ->
 *     6 MHz pixel clock, ~65 Hz (stock targets 70 Hz)
 *   - 4 bpp palettized instead of 16 bpp 5:6:5, so the 38.4 KB framebuffer
 *     fits in internal SRAM at 0x10080000 and SDRAM is not needed
 * Expect 16 colour bands (side by side as the unit is read: the panel is
 * mounted on its side) whose colours move along a few times a second. */
#include "lpc4350.h"

#define SMOKE_W   240
#define SMOKE_H   320
#define SMOKE_FB  0x10080000u               /* local SRAM bank 2 (72 KB) */

#define CGU_BASE_LCD_CLK   REG32(CGU_BASE + 0x088)
#define CCU1_CLK_M4_LCD    REG32(CCU1_BASE + 0x418)
#define LCD_TIMH    REG32(LCD_BASE + 0x000)
#define LCD_TIMV    REG32(LCD_BASE + 0x004)
#define LCD_POL     REG32(LCD_BASE + 0x008)
#define LCD_LE      REG32(LCD_BASE + 0x00C)
#define LCD_UPBASE  REG32(LCD_BASE + 0x010)
#define LCD_UPCURR  REG32(LCD_BASE + 0x02C)
#define LCD_CTRL    REG32(LCD_BASE + 0x018)
#define LCD_INTMSK  REG32(LCD_BASE + 0x01C)
#define LCD_PAL(i)  REG32(LCD_BASE + 0x200 + 4u*(i))

static void spin(volatile uint32_t n) { while (n--) { } }

#include "lcd_panel.h"

/* 16 entries of 1:5:5:5 (R low, G mid, B high), two per palette word. */
static void palette(unsigned rot) {
    static const uint16_t c[16] = {
        0x0000, 0x001F, 0x03E0, 0x7C00, 0x03FF, 0x7C1F, 0x7FE0, 0x7FFF,
        0x0010, 0x0200, 0x4000, 0x0210, 0x4010, 0x4200, 0x4210, 0x2108,
    };
    for (unsigned i = 0; i < 8; ++i)
        LCD_PAL(i) = c[(2*i + rot) & 15] | (uint32_t)c[(2*i + 1 + rot) & 15] << 16;
}

/* Bring the panel up showing the 16 bands. */
static void lcd_start(void) {
    lcd_pins();

    CGU_BASE_LCD_CLK = (1u << 24) | (1u << 11);     /* IRC 12 MHz, autoblock */
    CCU1_CLK_M4_LCD |= 1u;                          /* branch clock RUN      */

    uint8_t *fb = (uint8_t *)SMOKE_FB;              /* 4 bpp: 2 pixels/byte  */
    for (unsigned y = 0; y < SMOKE_H; ++y) {
        uint8_t band = (uint8_t)(y / (SMOKE_H / 16));
        for (unsigned x = 0; x < SMOKE_W / 2; ++x)
            fb[y * (SMOKE_W / 2) + x] = (uint8_t)(band | band << 4);
    }

    LCD_CTRL  &= ~1u;
    LCD_TIMH   = (19u << 24) | (9u << 16) | (9u << 8) | (((SMOKE_W / 16) - 1) << 2);
    LCD_TIMV   = (1u << 24) | (3u << 16) | (1u << 10) | (SMOKE_H - 1);
    LCD_POL    = ((SMOKE_W - 1) << 16) | (1u << 13) | (1u << 12) | (1u << 11) | 0x7C0;
    LCD_LE     = 0;
    LCD_INTMSK = 0;
    palette(0);
    LCD_UPBASE = SMOKE_FB;
    LCD_CTRL   = (2u << 1) | (1u << 5);             /* 4 bpp, TFT            */
    LCD_CTRL  |= 1u << 11;                          /* LcdPwr                */
    spin(1000000);
    LCD_CTRL  |= 1u;                                /* LcdEn                 */
    spin(3000000);
    lcd_panel_setup();                              /* ends with sleep-out      */
    spin(3000000);                                  /* >= 120 ms at any core clock the ROM leaves */
    lcd_panel_on();
}

#ifndef SMOKE_LCD_NO_MAIN
/* For a host that cannot see the panel: after about 15 s, if the controller
 * is fetching pixels and the panel's SPI port has gone idle, go back to the
 * DFU loader (the unit returns to USB as 1fc9:000c). If it does not come
 * back, one of the two failed. */
#include "dfu.h"
int main(void) {
    lcd_start();
    for (unsigned rot = 1;; ++rot) {
        spin(2000000);
        palette(rot);
        if (rot == 70) {
            uint32_t at = LCD_UPCURR, moved = 0;
            for (unsigned i = 0; i < 1000000 && !moved; ++i) moved = LCD_UPCURR != at;
            if (moved && !(SSP1_SR & (1u << 4)))
                dfu_reboot();
        }
    }
}
#endif
