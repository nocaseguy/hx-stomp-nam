/* init.c - LPC4350 clock + external SDRAM bring-up for the HX Stomp.
 *
 * CONFIRMED from the stock firmware: 12 MHz crystal, PLL1 x17 -> 204 MHz M4,
 * EMC SDRAM on DYCS0 at 0x28000000, and the full EMC register map + command
 * sequence (NOP -> PALL -> MODE -> normal). The exact SDRAM timing values and
 * the per-pin EMC mux live in data tables not traced from the binary, so the
 * values below are standard NXP LPC4350 32 MB (256 Mbit x16) settings. They are
 * correct in structure and safe as a starting point; TUNE against the actual
 * SDRAM part once the board is in hand. */
#include "init.h"
#include "lpc4350.h"
#include "delay.h"

#define CCU1_CLK_M4_EMC_CFG     REG32(CCU1_BASE+0x460)
#define CCU1_CLK_M4_EMCDIV_CFG  REG32(CCU1_BASE+0x468)
#define CREG6                   REG32(CREG_BASE+0x12C)

uint32_t clock_init(void) {
    /* 1. Enable the 12 MHz crystal oscillator (HF=0 for <20 MHz, enable). */
    CGU_XTAL_OSC_CTRL = 0x0;              /* clear PD(bit0)+HF(bit1) => running */
    for (volatile int i=0;i<300000;i++){} /* ~ >250us startup */

    /* 2. PLL1: source = crystal(0x06), M=17 (MSEL=16), N=1, direct, autoblock. */
    CGU_PLL1_CTRL = (0x06u<<24)|(16u<<16)|(1u<<11)|(1u<<7)|(1u<<6);
    while (!(CGU_PLL1_STAT & 1u)) { }     /* wait for lock */

    /* 3. Route BASE_M4_CLK to PLL1 (clksel 0x09), autoblock. */
    CGU_BASE_M4_CLK = (0x09u<<24)|(1u<<11);

    /* 4. EMC clock: run branch; EMC = M4/2 (102 MHz, conservative for SDRAM). */
    CREG6 |= (1u<<0);                     /* EMC_CLK_SEL = 1 -> divided (M4/2) */
    CCU1_CLK_M4_EMC_CFG    |= 1u;
    CCU1_CLK_M4_EMCDIV_CFG |= 1u;

    delay_set_clock(204000000u);
    return 204000000u;
}

/* Standard LPC4350 EMC pin set (function numbers per UM10503). Value 0xF2 =
 * FUNC (per pin) | EZI | ZIF | EHS, pull-up disabled - the recommended EMC pad
 * config. The stock firmware uses SCU values 0xF0-0xF5 on these pads. */
static void emc_pins(void) {
    /* Address A0..A14, Data D0..D15, and control (CS0,CAS,RAS,WE,CLK,CKE,DQM).
     * These are the fixed EMC-capable pads; func is the EMC alternate on each.
     * Encoded as {port,pin,func}. Kept compact; see UM10503 Table "EMC". */
    static const struct { uint8_t p,n,f; } m[] = {
        /* data D0..D7 on P1_7..P1_14 (func3) */
        {1,7,3},{1,8,3},{1,9,3},{1,10,3},{1,11,3},{1,12,3},{1,13,3},{1,14,3},
        /* data D8..D15 on P5_4..P5_7,P6_6.. (mix) */
        {5,4,2},{5,5,2},{5,6,2},{5,7,2},{6,6,1},{6,7,1},{6,8,1},{6,9,1},
        /* addr A0..A11 on P2_0..P2_2,P2_6..P2_9,P2_10..P2_13 (func3/2) */
        {2,0,3},{2,1,3},{2,2,3},{2,6,3},{2,7,3},{2,8,3},{2,9,3},
        {2,10,3},{2,11,3},{2,12,3},{2,13,3},{1,0,2},
        /* control: OE P1_3(f3), WE P1_6(f3), CS0 P6_9?/CAS/RAS/CLK/CKE/DQM */
        {1,3,3},{1,6,3},{6,4,3},{6,5,3},{6,11,3},{6,12,3},
    };
    for (unsigned i=0;i<sizeof(m)/sizeof(m[0]);++i)
        SCU_SFS(m[i].p,m[i].n) = SCU_FUNC(m[i].f)|SCU_EZI|SCU_ZIF|SCU_EHS|SCU_EPUN;
}

int sdram_init(void) {
    emc_pins();

    REG32(EMC_BASE+0x000) = 1;            /* EMC Control: enable, little-endian */
    delay_ms(1);

    /* Dynamic memory timing (standard 256Mbit x16 @<=104 MHz EMC clock). */
    REG32(EMC_BASE+0x028) = 0x00000001;   /* ReadConfig: command-delayed        */
    REG32(EMC_BASE+0x030) = 1;            /* tRP  */
    REG32(EMC_BASE+0x034) = 3;            /* tRAS */
    REG32(EMC_BASE+0x038) = 5;            /* tSREX*/
    REG32(EMC_BASE+0x03C) = 0;            /* tAPR */
    REG32(EMC_BASE+0x040) = 4;            /* tDAL */
    REG32(EMC_BASE+0x044) = 1;            /* tWR  */
    REG32(EMC_BASE+0x048) = 5;            /* tRC  */
    REG32(EMC_BASE+0x04C) = 5;            /* tRFC */
    REG32(EMC_BASE+0x050) = 5;            /* tXSR */
    REG32(EMC_BASE+0x05C) = 1;            /* tRRD? (offset varies) */
    /* geometry: 256Mb, 4 banks, 13 rows, 9 cols, 16-bit, RBC addressing */
    REG32(EMC_BASE+0x100) = 0x00005680;   /* DynamicConfig0  */
    REG32(EMC_BASE+0x104) = 0x00000303;   /* RAS=3, CAS=3    */

    /* SDRAM init command sequence via DynamicControl (EMC+0x020). */
    REG32(EMC_BASE+0x020) = 0x00000183;   /* CE+CS, NOP */
    delay_ms(1);
    REG32(EMC_BASE+0x020) = 0x00000103;   /* PALL */
    REG32(EMC_BASE+0x024) = 0x00000001;   /* fast refresh for init bursts */
    for (volatile int i=0;i<0x400;i++){}
    REG32(EMC_BASE+0x024) = 0x00000046;   /* normal refresh (~7.8us @102MHz)  */
    REG32(EMC_BASE+0x020) = 0x00000083;   /* MODE */
    /* mode register load: read from base + (CL3,BL4 mode word << bank/col shift) */
    (void)REG32(SDRAM_BASE + (0x33 << 12));
    REG32(EMC_BASE+0x020) = 0x00000000;   /* NORMAL operation */
    REG32(EMC_BASE+0x100) |= (1u<<19);    /* enable dynamic buffer */
    delay_ms(1);
    return 0;
}
