#include "display.h"
#include "lpc4350.h"
#include "delay.h"

#ifdef HX_SIM
uint16_t sim_fb[LCD_W*LCD_H];
uint16_t *display_fb(void){ return sim_fb; }
void display_init(void) { }
void display_px(int x, int y, uint16_t c){
    if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H) sim_fb[y*LCD_W+x] = c;
}
#else
#include "lcd_panel.h"

#define SCAN_W 240                          /* as the controller scans it */
#define SCAN_H 320
#define CGU_BASE_LCD_CLK   REG32(CGU_BASE + 0x088)
#define CCU1_CLK_M4_LCD    REG32(CCU1_BASE + 0x418)
#define LCD_TIMH    REG32(LCD_BASE + 0x000)
#define LCD_TIMV    REG32(LCD_BASE + 0x004)
#define LCD_POL     REG32(LCD_BASE + 0x008)
#define LCD_LE      REG32(LCD_BASE + 0x00C)
#define LCD_UPBASE  REG32(LCD_BASE + 0x010)
#define LCD_CTRL    REG32(LCD_BASE + 0x018)
#define LCD_INTMSK  REG32(LCD_BASE + 0x01C)
#define CORE_HZ     204000000u                              /* PLL1 after clock_init() */
#define PIXEL_HZ    (70u * (SCAN_W + 40) * (SCAN_H + 8))    /* 70 Hz, as stock */

static uint16_t *const fb = (uint16_t *)(LCD_FB0 & ~0x7u);

void display_init(void) {
    lcd_pins();
    CGU_BASE_LCD_CLK = (9u << 24) | (1u << 11);     /* PLL1, autoblock */
    CCU1_CLK_M4_LCD |= 1u;
    display_clear(0);

    uint32_t pcd = CORE_HZ / PIXEL_HZ - 2;          /* pixel clock = CORE_HZ / (pcd + 2) */
    LCD_CTRL  &= ~1u;
    LCD_TIMH   = (19u << 24) | (9u << 16) | (9u << 8) | (((SCAN_W / 16) - 1) << 2);
    LCD_TIMV   = (1u << 24) | (3u << 16) | (1u << 10) | (SCAN_H - 1);
    LCD_POL    = (pcd >> 5) << 27 | ((SCAN_W - 1) << 16) | (1u << 13) | (1u << 12) | (1u << 11) | (pcd & 31u);
    LCD_LE     = 0;
    LCD_INTMSK = 0;
    LCD_UPBASE = (uint32_t)fb;
    LCD_CTRL   = (6u << 1) | (1u << 5) | (1u << 8); /* 16 bpp 5:6:5, TFT, red in the high bits */
    LCD_CTRL  |= 1u << 11;                          /* LcdPwr */
    delay_ms(20);
    LCD_CTRL  |= 1u;                                /* LcdEn  */
    delay_ms(120);
    lcd_panel_setup();
    delay_ms(120);
    lcd_panel_on();
}

/* The panel's lines run left to right as it is read, its columns bottom to top. */
void display_px(int x, int y, uint16_t c){
    if (x >= 0 && x < LCD_W && y >= 0 && y < LCD_H) fb[x*SCAN_W + (SCAN_W - 1 - y)] = c;
}
#endif

void display_fill_rect(int x,int y,int w,int h,uint16_t c){
    for (int j=y;j<y+h;j++)
        for (int i=x;i<x+w;i++)
            display_px(i, j, c);
}
void display_clear(uint16_t c){
    display_fill_rect(0, 0, LCD_W, LCD_H, c);
}
