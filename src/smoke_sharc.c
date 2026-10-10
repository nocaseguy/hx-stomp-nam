/* smoke_sharc.c - boot our own SHARC program and show that it runs.
 *
 * LCD as in smoke_lcd.c (no PLL, no SDRAM), then the SHARC is reset and given
 * build/sharc_hello.h (src/sharc/kernel.asm + hello.asm) over SSP0.
 *
 * Wiring and timing are the stock firmware's (tools/sharc/HARDWARE.md):
 *   SSP0: SCK P3_0 (func 4), MISO P9_1 (func 7), MOSI P9_2 (func 7), SPI mode 3
 *   chip select GPIO4[12] (P9_0), low for the whole download
 *   SHARC reset GPIO7[1] (PE_1), active low, 10 ms low then 10 ms high
 *   SHARC -> host line GPIO7[0] (PE_0)
 *   every 32-bit word goes out least significant bit first
 * Ours: 2 MHz from the 12 MHz IRC instead of 20 MHz, and a pause after each
 * record so the kernel can re-arm its receiver.
 *
 * What to look for, top to bottom on the panel:
 *   bands 0-3    stage reached: all four lit = download finished
 *   bands 4-7    level of the SHARC line right now (lit = high)
 *   bands 8-15   count of edges seen on that line, in binary, band 8 = bit 0
 * hello.asm toggles FLAG2 and FLAG3, so a running program makes bands 4-7
 * blink and the counter climb; the stock program never does that. */
#define SMOKE_LCD_NO_MAIN
#include "smoke_lcd.c"
#ifndef SMOKE_SHARC_NO_MAIN
#include "../build/sharc_hello.h"
#endif

#define SSP0_CR0   REG32(SSP0_BASE + 0x00)
#define SSP0_CR1   REG32(SSP0_BASE + 0x04)
#define SSP0_DR    REG32(SSP0_BASE + 0x08)
#define SSP0_SR    REG32(SSP0_BASE + 0x0C)
#define SSP0_CPSR  REG32(SSP0_BASE + 0x10)
#define CCU1_CLK_M4_SSP0    REG32(CCU1_BASE + 0x518)
#define CCU2_CLK_APB0_SSP0  REG32(CCU2_BASE + 0x700)

#define CS_PORT 4
#define CS_PIN  12
#define DSP_PORT 7
#define DSP_RESET 1
#define DSP_LINE  0

#define KERNEL_WORDS 384
#define MS 9600u                              /* spin() passes per ms if the boot ROM left the core
                                                 at 96 MHz; eight times longer at 12 MHz, which is fine */

static uint16_t col[16];

static void show(void) {
    for (unsigned i = 0; i < 8; ++i)
        LCD_PAL(i) = col[2*i] | (uint32_t)col[2*i + 1] << 16;
}

static void bands(unsigned first, unsigned n, uint16_t c) {
    for (unsigned i = 0; i < n; ++i) col[first + i] = c;
    show();
}

static uint16_t rev16(uint16_t v) {
    v = (uint16_t)((v & 0x5555u) << 1 | (v >> 1 & 0x5555u));
    v = (uint16_t)((v & 0x3333u) << 2 | (v >> 2 & 0x3333u));
    v = (uint16_t)((v & 0x0F0Fu) << 4 | (v >> 4 & 0x0F0Fu));
    return (uint16_t)(v << 8 | v >> 8);
}

static void frame(uint16_t v) {
    SSP0_DR = v;
    while (!(SSP0_SR & (1u << 2))) { }        /* RNE: the frame has gone out */
    (void)SSP0_DR;
}

/* SSP shifts the most significant bit first; the SHARC wants the least. */
static void send(uint32_t w) {
    frame(rev16((uint16_t)w));
    frame(rev16((uint16_t)(w >> 16)));
}

static void link_init(void) {
    SCU_SFS(3, 0)  = 0x24;                    /* SSP0_SCK                   */
    SCU_SFS(9, 1)  = 0xE7;                    /* SSP0_MISO                  */
    SCU_SFS(9, 2)  = 0x27;                    /* SSP0_MOSI                  */
    SCU_SFS(9, 0)  = 0x00;                    /* GPIO4[12]: chip select     */
    SCU_SFS(14, 0) = 0x44;                    /* GPIO7[0]: line from SHARC  */
    SCU_SFS(14, 1) = 0x04;                    /* GPIO7[1]: SHARC reset      */
    GPIO_SET(CS_PORT)  = 1u << CS_PIN;
    GPIO_DIR(CS_PORT) |= 1u << CS_PIN;
    GPIO_DIR(DSP_PORT) = (GPIO_DIR(DSP_PORT) & ~(1u << DSP_LINE)) | 1u << DSP_RESET;

    CGU_BASE_SSP0_CLK   = (1u << 24) | (1u << 11);      /* IRC 12 MHz, autoblock */
    CCU1_CLK_M4_SSP0   |= 1u;
    CCU2_CLK_APB0_SSP0 |= 1u;
    SSP0_CR1  = 0;
    SSP0_CPSR = 2;
    SSP0_CR0  = 15u | 1u << 6 | 1u << 7 | 2u << 8;      /* 16 bit, CPOL, CPHA, 12/2/3 = 2 MHz */
    SSP0_CR1  = 1u << 1;                                /* enable, master */
}

static void sharc_boot(const uint32_t *img, unsigned n) {
    GPIO_CLR(DSP_PORT) = 1u << DSP_RESET;
    spin(10 * MS);
    GPIO_SET(DSP_PORT) = 1u << DSP_RESET;
    spin(10 * MS);
    bands(0, 1, 0x7FFF);                      /* stage 1: out of reset */

    GPIO_CLR(CS_PORT) = 1u << CS_PIN;
    spin(MS);
    unsigned i = 0;
    for (; i < KERNEL_WORDS; ++i) send(img[i]);
    spin(10 * MS);
    bands(1, 1, 0x7FFF);                      /* stage 2: kernel sent */

    while (i + 2 <= n) {
        uint32_t count = img[i + 1];
        send(img[i]); send(count);
        i += 2;
        spin(MS);
        if (count == 0) break;
        for (; count && i < n; --count) send(img[i++]);
        spin(MS);
    }
    bands(2, 1, 0x7FFF);                      /* stage 3: program sent */
    spin(MS);
    GPIO_SET(CS_PORT) = 1u << CS_PIN;
    bands(3, 1, 0x7FFF);                      /* stage 4: link released */
}

/* A message to a running program that listens the way the boot kernel does
 * (nam_live.asm): two words under their own chip select. */
__attribute__((unused)) static void sharc_message(uint32_t a, uint32_t b) {
    GPIO_CLR(CS_PORT) = 1u << CS_PIN;
    spin(MS / 4);
    send(a);
    send(b);
    spin(MS / 4);
    GPIO_SET(CS_PORT) = 1u << CS_PIN;
    spin(3 * MS);                             /* time to act on it and listen again */
}

#ifndef SMOKE_SHARC_NO_MAIN
int main(void) {
    lcd_start();
    bands(0, 16, 0);
    link_init();
    sharc_boot(sharc_hello, sizeof sharc_hello / sizeof sharc_hello[0]);

    unsigned last = GPIO_PIN(DSP_PORT) >> DSP_LINE & 1u, edges = 0;
    for (;;) {
        unsigned now = GPIO_PIN(DSP_PORT) >> DSP_LINE & 1u;
        if (now != last) { last = now; ++edges; }
        bands(4, 4, now ? 0x03E0 : 0);
        for (unsigned b = 0; b < 8; ++b) col[8 + b] = edges >> b & 1u ? 0x7FE0 : 0x0010;
        show();
    }
}
#endif
