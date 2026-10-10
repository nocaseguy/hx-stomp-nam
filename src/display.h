/* display.h - the HX Stomp XL's screen for the main firmware.
 *
 * The LCD controller scans the panel 240 wide by 320 lines, 16 bpp 5:6:5, and
 * the panel is mounted on its side. This driver presents it the way it is
 * read: 320 wide, 240 high, (0,0) top left. Controller values, pins, panel
 * set-up and backlight are the ones that work in smoke_lcd.c (lcd_panel.h);
 * what is specific to this driver - pixel clock from PLL1, 16 bpp, the
 * framebuffer in SDRAM, the colour order - has not run on the unit yet. */
#ifndef HX_DISPLAY_H
#define HX_DISPLAY_H
#include <stdint.h>

#define LCD_W        320
#define LCD_H        240
#define LCD_FB0      0x29F00CA8u    /* where the stock firmware keeps its first buffer */

#define RGB565(r,g,b) ((uint16_t)(((r&0xF8)<<8)|((g&0xFC)<<3)|((b)>>3)))

void display_init(void);                 /* needs clock_init() and sdram_init() */
void display_clear(uint16_t color);
void display_px(int x, int y, uint16_t color);
void display_fill_rect(int x,int y,int w,int h,uint16_t color);
#ifdef HX_SIM
uint16_t *display_fb(void);              /* LCD_W x LCD_H, row by row */
#endif

#endif
