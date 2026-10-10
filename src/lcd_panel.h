/* lcd_panel.h - what the HX Stomp XL's panel needs besides the LCD controller,
 * shared by smoke_lcd.c and display.c. Include after lpc4350.h.
 *
 * All of it is read from the stock bootloader and works on the unit:
 *   - LCD pins: P4_1..P4_6, P4_8..P4_10 = 0x32, P4_7 = 0x30, P7_0..P7_7 = 0x33,
 *     P8_3..P8_7 = 0x33, PB_0..PB_6 = 0x32
 *   - backlight: GPIO7[14] (PE_14 = 0x14), active low; held high until the
 *     panel is set up
 *   - GPIO6[28] (PD_14 = 0x34) driven low: the first thing the bootloader and
 *     the updater do. What it switches is not known; the screen is dark
 *     without it. The bootloader's three other low outputs (PA_1, PA_2, P6_7)
 *     are not needed for the screen.
 *   - the panel (ILI9341 command set) is configured over SSP1 in 9-bit
 *     frames, bit 8 = data, SPI mode 0 at 1 MHz: SSEL P1_5 = 0x35,
 *     SCK P1_19 = 0x31, MISO P0_0 = 0x71, MOSI P0_1 = 0x31. The sequence
 *     is sent 120 ms after LcdEn and ends with
 *     sleep-out, a pause, display-on. Without it the panel stays dark.
 * Order: lcd_pins(); controller set-up, LcdPwr, wait, LcdEn; wait 120 ms;
 * lcd_panel_setup(); wait 120 ms; lcd_panel_on(). */
#define SSP1_CR0    REG32(SSP1_BASE + 0x00)
#define SSP1_CR1    REG32(SSP1_BASE + 0x04)
#define SSP1_DR     REG32(SSP1_BASE + 0x08)
#define SSP1_SR     REG32(SSP1_BASE + 0x0C)
#define SSP1_CPSR   REG32(SSP1_BASE + 0x10)
#define CCU1_CLK_M4_SSP1    REG32(CCU1_BASE + 0x628)
#define CCU2_CLK_APB2_SSP1  REG32(CCU2_BASE + 0x600)

static void lcd_pins(void) {
    static const struct { uint8_t port, pin, val; } m[] = {
        {4,1,0x32},{4,2,0x32},{4,3,0x32},{4,4,0x32},{4,5,0x32},{4,6,0x32},
        {4,7,0x30},{4,8,0x32},{4,9,0x32},{4,10,0x32},
        {7,0,0x33},{7,1,0x33},{7,2,0x33},{7,3,0x33},
        {7,4,0x33},{7,5,0x33},{7,6,0x33},{7,7,0x33},
        {8,3,0x33},{8,4,0x33},{8,5,0x33},{8,6,0x33},{8,7,0x33},
        {11,0,0x32},{11,1,0x32},{11,2,0x32},{11,3,0x32},
        {11,4,0x32},{11,5,0x32},{11,6,0x32},
        {14,14,0x14},                       /* PE_14 -> GPIO7[14] */
    };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; ++i)
        SCU_SFS(m[i].port, m[i].pin) = m[i].val;
    GPIO_DIR(7) |= 1u << 14;
    GPIO_SET(7)  = 1u << 14;
    SCU_SFS(13, 14) = 0x34;                 /* PD_14 -> GPIO6[28], low: without it the screen stays dark */
    GPIO_CLR(6)  = 1u << 28;
    GPIO_DIR(6) |= 1u << 28;
}

/* The stock panel set-up: commands, and parameters with bit 8 set. */
static const uint16_t panel_seq[] = {
    0x0CF, 0x100, 0x1D9, 0x130,
    0x0ED, 0x164, 0x103, 0x112, 0x181,
    0x0E8, 0x185, 0x110, 0x17A,
    0x0CB, 0x139, 0x12C, 0x100, 0x134, 0x102,
    0x0F7, 0x120,
    0x0EA, 0x100,
    0x0C0, 0x121,
    0x0C1, 0x111,
    0x0C5, 0x123, 0x10A,
    0x0C7, 0x1C0,
    0x03A, 0x166,                               /* 18 bits a pixel            */
    0x036, 0x148,
    0x0B0, 0x1C0,                               /* RGB interface              */
    0x0B6, 0x10A, 0x182, 0x127, 0x104,
    0x0F6, 0x101, 0x100, 0x106,
    0x0B5, 0x102, 0x102, 0x10A, 0x13C,
    0x0F2, 0x102,
    0x026, 0x101,
    0x0E0, 0x10F, 0x127, 0x124, 0x10C, 0x110, 0x108, 0x155, 0x187,
           0x145, 0x108, 0x114, 0x107, 0x113, 0x108, 0x100,
    0x0E1, 0x100, 0x10F, 0x112, 0x105, 0x111, 0x106, 0x125, 0x134,
           0x137, 0x101, 0x108, 0x107, 0x12B, 0x134, 0x10F,
};

static void panel_send(uint16_t w) {
    while (!(SSP1_SR & (1u << 1))) { }              /* room in the transmit FIFO */
    SSP1_DR = w;
}

/* Configure the panel over SSP1 and take it out of sleep. Wait 120 ms, then
 * call lcd_panel_on(). */
static void lcd_panel_setup(void) {
    SCU_SFS(1, 5)  = 0x35;                          /* SSP1_SSEL */
    SCU_SFS(1, 19) = 0x31;                          /* SSP1_SCK  */
    SCU_SFS(0, 0)  = 0x71;                          /* SSP1_MISO */
    SCU_SFS(0, 1)  = 0x31;                          /* SSP1_MOSI */
    CGU_BASE_SSP1_CLK   = (1u << 24) | (1u << 11);  /* IRC 12 MHz, autoblock */
    CCU1_CLK_M4_SSP1   |= 1u;
    CCU2_CLK_APB2_SSP1 |= 1u;
    SSP1_CR1  = 0;
    SSP1_CPSR = 2;
    SSP1_CR0  = 8u | 5u << 8;                       /* 9 bit, mode 0, 12/2/6 = 1 MHz */
    SSP1_CR1  = 1u << 1;
    for (unsigned i = 0; i < sizeof panel_seq / sizeof panel_seq[0]; ++i)
        panel_send(panel_seq[i]);
    panel_send(0x011);                              /* sleep out  */
}

static void lcd_panel_on(void) {
    panel_send(0x029);                              /* display on */
    GPIO_CLR(7) = 1u << 14;                         /* backlight on (active low) */
}
