/* sdram.h - the HX Stomp's SDRAM for the images loaded into RAM: 32 MB at
 * 0x28000000, two 128 Mbit x16 parts on a 32-bit bus. Include after
 * lpc4350.h and something with spin() (smoke_lcd.c).
 *
 * Everything here but the clock is the stock firmware's own set-up, read
 * from its bootloader and found again, with the numbers worked out, in its
 * updater: the pins and their pad values, the order of the writes, the mode
 * word, the pad delays.
 *
 * The clock is where this differs. Stock first takes the core to 204 MHz and
 * runs the EMC at half of that, 102 MHz. The images here leave the core clock
 * as the boot ROM set it, because their own timing leans on it (pedal.c's
 * waits, the reading of the SHARC's frames). So this measures the core clock
 * against the 12 MHz IRC, runs the EMC at half of it as stock does, and works
 * the timing registers out for that rate with the bootloader's own sum:
 * nanoseconds x MHz / 1000, cut down to a whole number. A slower clock than
 * stock's only makes the memory's waits longer in time.
 *
 * Works on the unit: the ROM leaves the core at 96 MHz, so the EMC runs at 48. */
#ifndef SDRAM_H
#define SDRAM_H

#ifndef SDRAM_STEP                      /* smoke_sdram.c says where it has got to */
#define SDRAM_STEP(n) ((void)0)
#endif

#define EMC(o)              REG32(EMC_BASE + (o))
#define CGU_FREQ_MON        REG32(CGU_BASE + 0x014)
#define CREG6               REG32(CREG_BASE + 0x12C)
#define CCU1_CLK_M4_EMCDIV  REG32(CCU1_BASE + 0x478)
#define SCU_SFSCLK(n)       REG32(SCU_BASE + 0xC00 + 4u * (n))
#define SCU_EMCDELAYCLK     REG32(SCU_BASE + 0xD00)

static unsigned sdram_core_mhz, sdram_emc_mhz;          /* as measured and as set, for whoever reports */
static uint32_t sdram_bad_at, sdram_bad_wrote, sdram_bad_read;      /* where sdram_test() gave up */

/* The core clock in MHz, or 0 if the measurement does not end: the CGU counts
 * the clock BASE_M4_CLK is fed from over 511 cycles of the IRC. */
static unsigned sdram_measure(void) {
    unsigned source = CGU_BASE_M4_CLK >> 24 & 0x1Fu;
    if (source == 1) return 12;                                 /* the IRC itself */
    CGU_FREQ_MON = source << 24 | 1u << 23 | 511u;
    for (unsigned n = 0; CGU_FREQ_MON & 1u << 23; ++n) if (n > 4000000u) return 0;
    uint32_t m = CGU_FREQ_MON;
    unsigned counted = m >> 9 & 0x3FFFu, irc = 511u - (m & 0x1FFu);        /* it stops early if the count fills */
    return irc ? (counted * 12u + irc / 2) / irc : 0;
}

/* Set the SDRAM up. Returns 0 if the core clock could not be measured. */
static int sdram_start(void) {
    static const struct { uint8_t port, first, last, val; } pin[] = {
        {1, 7, 14, 0xF3},                       /* D0-D7 */
        {5, 4, 7, 0xF2}, {5, 0, 3, 0xF2},       /* D8-D11, D12-D15 */
        {13, 2, 9, 0xF2},                       /* D16-D23 */
        {14, 5, 12, 0xF3},                      /* D24-D31 */
        {2, 9, 13, 0xF3},                       /* A0-A4 */
        {1, 0, 2, 0xF2},                        /* A5-A7 */
        {2, 8, 8, 0xF3}, {2, 7, 7, 0xF3}, {2, 6, 6, 0xF2},      /* A8, A9, A10 */
        {2, 0, 2, 0xF2},                        /* A13, A12, A11 */
        {6, 8, 8, 0xF1},                        /* A14 */
        {1, 6, 6, 0xF3},                        /* WE */
        {6, 4, 5, 0xF3},                        /* CAS, RAS */
        {6, 10, 12, 0xF3},                      /* DQMOUT1, CKEOUT0, DQMOUT0 */
        {14, 13, 13, 0xF3}, {13, 0, 0, 0xF2},   /* DQMOUT3, DQMOUT2 */
        {6, 9, 9, 0xF3},                        /* DYCS0 */
    };
    /* the memory's times in nanoseconds, in the order of the registers from EMC + 0x30 */
    static const uint8_t ns[11] = { 18, 44, 74, 10, 44, 22, 62, 62, 74, 14, 22 };

    SDRAM_STEP(1);
    sdram_core_mhz = sdram_measure();
    if (!sdram_core_mhz) return 0;
    SDRAM_STEP(2);
    unsigned mhz = sdram_emc_mhz = sdram_core_mhz / 2;

    /* stock does this just before: P6_7 as GPIO5[15], driven low. What it switches is not known. */
    SCU_SFS(6, 7) = 0x14;
    GPIO_DIR(5) |= 1u << 15;
    GPIO_CLR(5)  = 1u << 15;

    SDRAM_STEP(3);
    EMC(0x000) = 0;                             /* off while it is set up */
    CREG6 |= 1u << 16;                          /* the EMC's clock: the divided one */
    CCU1_CLK_M4_EMCDIV = 0x21;                  /* run, core / 2 */
    SDRAM_STEP(4);
    SCU_EMCDELAYCLK = 0x5555;
    SCU_SFSCLK(0) = 0xF5; SCU_SFSCLK(1) = 0xF0; SCU_SFSCLK(2) = 0xF5; SCU_SFSCLK(3) = 0xF0;
    for (unsigned i = 0; i < sizeof pin / sizeof pin[0]; ++i)
        for (unsigned p = pin[i].first; p <= pin[i].last; ++p) SCU_SFS(pin[i].port, p) = pin[i].val;

    SDRAM_STEP(5);
    EMC(0x000) = 1;
    EMC(0x100) = 0x00004480;                    /* 128 Mbit x16, 4 banks, 12 rows, 9 columns; 32-bit bus */
    EMC(0x104) = 0x00000202;                    /* RAS 2, CAS 2 */
    EMC(0x028) = 1;                             /* read on the command-delayed clock */
    for (unsigned i = 0; i < 11; ++i)           /* tRP tRAS tSREX tAPR tDAL tWR tRC tRFC tXSR tRRD tMRD */
        EMC(0x030 + 4 * i) = ns[i] * mhz / 1000 + (i == 4);
    EMC(0x024) = (15625u * mhz / 1000 + 15) >> 4;       /* a refresh every 15.6 us, in sixteens of clocks */
    SDRAM_STEP(6);
    EMC(0x020) = 0x183; spin(2000000);          /* NOP */
    EMC(0x020) = 0x103; spin(2000000);          /* precharge all */
    SDRAM_STEP(7);
    EMC(0x020) = 0x083;                         /* mode: CAS 2, bursts of 4, on the address lines */
    (void)*(volatile uint8_t *)(SDRAM_BASE + (0x22u << 13));
    spin(2000000);
    EMC(0x020) = 0x003;                         /* normal */
    EMC(0x100) = 0x00084480;                    /* buffers on */
    SDRAM_STEP(8);
    return 1;
}

/* Words that depend on their address, written over `bytes` at `base` and
 * read back, then the same turned over. 1 if all came back. */
static int sdram_test(uint32_t base, uint32_t bytes) {
    volatile uint32_t *m = (volatile uint32_t *)base;
    for (uint32_t turn = 0; turn < 2; ++turn) {
        uint32_t flip = turn ? 0xFFFFFFFFu : 0;
        for (uint32_t i = 0; i < bytes / 4; ++i) m[i] = (i * 0x9E3779B1u + 0x1234567u) ^ flip;
        for (uint32_t i = 0; i < bytes / 4; ++i) {
            uint32_t want = (i * 0x9E3779B1u + 0x1234567u) ^ flip, got = m[i];
            if (got == want) continue;
            sdram_bad_at = base + 4 * i; sdram_bad_wrote = want; sdram_bad_read = got;
            return 0;
        }
    }
    return 1;
}

#endif
