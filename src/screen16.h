/* screen16.h - the HX Stomp's screen in full colour: 16 bits a pixel, 5:6:5,
 * from the SDRAM. Include after smoke_lcd.c (which has brought the panel up
 * at 4 bits a pixel) and sdram.h (which must have the memory working), with
 * SCREEN_SCAN0, SCREEN_SCAN1 and FB_BACK naming three places in it for
 * 320 x 240 pixels of two bytes.
 *
 * The controller scans the panel 240 wide by 320 lines and the panel is
 * mounted on its side: its line number runs left to right as the unit is
 * read, its columns bottom to top. The UI draws the screen as it is read
 * into FB_BACK (src/ui/fb.h); screen_flip() turns that into scan order in
 * the buffer that is not being shown, then has the controller show it from
 * its next frame on.
 *
 * Timings, pins and pixel clock stay as smoke_lcd.c set them: 6 MHz from the
 * 12 MHz IRC, so a frame is 280 x 328 clocks = 15.307 ms whatever the core
 * clock is. screen_tick() counts frames, and that is the pedal's clock.
 *
 * Works on the unit, colours as named (smoke_sdram.c's bars). Turning the
 * controller off to change the mode leaves the panel white until the panel
 * is set up again: screen_start() does that. */
#ifndef SCREEN16_H
#define SCREEN16_H

#define LCD_INTRAW  REG32(LCD_BASE + 0x020)
#define LCD_INTCLR  REG32(LCD_BASE + 0x028)
#define SCREEN_FRAME_US 15307u

static uint16_t *const screen_scan[2] = { (uint16_t *)SCREEN_SCAN0, (uint16_t *)SCREEN_SCAN1 };
static unsigned screen_shown;                   /* which of the two the controller has */
static uint32_t screen_frames;                  /* frames the panel has shown since screen_start() */

/* To be called more often than every 15 ms: counts the panel's frames. */
__attribute__((unused)) static void screen_tick(void) {
    if (!(LCD_INTRAW & 1u << 3)) return;        /* the vertical compare has not come round */
    LCD_INTCLR = 1u << 3;
    ++screen_frames;
}
__attribute__((unused)) static uint32_t screen_ms(void) {
    return screen_frames * 15u + screen_frames * (SCREEN_FRAME_US - 15000u) / 1000u;
}

static void screen_start(void) {
    for (unsigned i = 0; i < SMOKE_W * SMOKE_H; ++i) screen_scan[0][i] = screen_scan[1][i] = 0;
    LCD_CTRL &= ~1u;                            /* off while the mode changes */
    spin(3000000);
    LCD_UPBASE = (uint32_t)screen_scan[0];
    LCD_CTRL = (6u << 1) | (1u << 5) | (1u << 8) | (1u << 11);     /* 16 bpp 5:6:5, TFT, red in the high bits, power */
    LCD_CTRL |= 1u;
    spin(3000000);                              /* the panel shows white once its signals have stopped, */
    lcd_panel_setup();                          /* until it is set up again as at the start */
    spin(3000000);
    lcd_panel_on();
    LCD_INTCLR = 1u << 3;
    screen_shown = 0;
}

#ifndef SCREEN_YIELD
#define SCREEN_YIELD() ((void)0)
#endif

static void screen_flip(void) {
    uint16_t *scan = screen_scan[screen_shown ^ 1u];
    for (unsigned x = 0; x < SMOKE_H; ++x) {    /* a line of the panel is a column of the picture, read upward */
        uint16_t *d = scan + x * SMOKE_W;
        const uint16_t *s = FB_BACK + (SMOKE_W - 1) * SMOKE_H + x;
        for (unsigned y = 0; y < SMOKE_W; ++y, s -= SMOKE_H) *d++ = *s;
        if ((x & 15u) == 15u) SCREEN_YIELD();
    }
    screen_shown ^= 1u;
    LCD_UPBASE = (uint32_t)scan;                /* taken at the start of the controller's next frame */
}

#endif
