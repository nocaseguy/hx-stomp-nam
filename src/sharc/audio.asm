// audio.asm - bring the HX Stomp's audio hardware up from the SHARC.
//
// audio_init does what the stock program does, with its values
// (tools/sharc/HARDWARE.md), in the order that matters:
//   1. signal routing: serial port clocks and frame syncs from the clock
//      generators, data pins, clock outputs to the codecs; and the second
//      routing unit, which holds DPI pin 7 - the codecs' reset - low
//   2. the two clock generators, from the 49.152 MHz on DAI pin 7: domain A
//      192 kHz, domain B 48 kHz, 64 bit clocks a frame
//   3. a wait, and DPI pin 7 high: the three CS4272 codecs (stand-alone mode)
//      must be in reset until their clocks are stable, or they stay powered
//      down with silent inputs and mute outputs
// and audio_ports then turns the serial ports on, I2S, 32-bit words, without
// DMA: receive SPORT0 A and B and SPORT1 A (48 kHz) and SPORT2 A (192 kHz),
// transmit SPORT4 A and B and SPORT6 A (48 kHz) and SPORT5 A (192 kHz). A
// program that wants DMA sets the ports up itself (nam_live.asm).
// It works on the unit: the inputs deliver samples and what is sent is heard
// on the main outputs, the sends and the headphones. The LPC4350 has to
// switch the bypass relays for the jacks to carry it (src/audio_pins.h).
//
// A word is waiting to be read when control bits 31:30 (A) or 28:27 (B) are
// not zero; there is no room to write when they are both set. Data registers:
// receive at control + 0x61 (A) and + 0x63 (B), transmit at + 0x60 and + 0x62.
//
// Assembled after report.asm, whose wait it uses; registers R0-R2, R4, R7,
// I0, I4, L0, L4, M0, M1, USTAT1.

.equ SPCTL0,  0xc00
.equ SPCTL1,  0xc01
.equ SPCTL2,  0x400
.equ RXSP0A,  0xc61
.equ RXSP0B,  0xc63
.equ RXSP1A,  0xc65
.equ RXSP2A,  0x461
.equ RX_A,    0x000029f1        // I2S, 32 bits, clock and frame sync from outside, receive, no DMA
.equ RX_AB,   0x010029f1        // and the B channel
.equ TX_A,    0x020029f1        // the same, transmit
.equ TX_AB,   0x030029f1
.equ SPCTL4,  0x800
.equ SPCTL5,  0x801
.equ SPCTL6,  0x4800
.equ TXSP4A,  0x860
.equ TXSP4B,  0x862
.equ TXSP5A,  0x864
.equ TXSP6A,  0x4860

.section dm 0xc0600
AU_SRU:                          // register, bits to keep, bits to set
        .word 0x2430, 0xc00f8000, 0x39d073bd    // SPORT0,1,4 clock = PCG B; SPORT2,5 clock = PCG A
        .word 0x2450, 0xc00f8000, 0x39d073bd    // the same for the frame syncs
        .word 0x2435, 0xffffffe0, 0x0000001d    // SPORT6 clock = PCG B
        .word 0x2454, 0xffffffe0, 0x0000001d    // SPORT6 frame sync = PCG B
        .word 0x2434, 0xfffffc00, 0x000000c6    // PCG A and B external clock = DAI pin 7
        .word 0x2440, 0xc0fc0000, 0x0e005491    // SPORT0 A,B in = DAI 18,19; SPORT1 A = 6; SPORT2 A = 15
        .word 0x2460, 0xf0000000, 0x074b0e9c    // DAI 1,2 = SPORT4 A,B out; 4 = PCG frame sync A
        .word 0x2461, 0xf0000000, 0x0fdfbf7e
        .word 0x2462, 0xf0000000, 0x0fcedcb8    // DAI 9 = PCG clock A, 10 = clock B, 11 = frame sync B
        .word 0x2463, 0xf0000000, 0x0fdfbf7e
        .word 0x2464, 0xf0000000, 0x03dfbf38    // DAI 17 = PCG clock A, 20 = SPORT5 A out
        .word 0x2478, 0xc0000000, 0x00041041    // output enables
        .word 0x2479, 0xc0000000, 0x01040000
        .word 0x247a, 0xc0000000, 0x00000001
        .word 0x247b, 0xc0000000, 0x01000040
        .word 0x1c00, 0xfe0003e0, 0x00221402    // the second routing unit (DPI pins), also as stock:
        .word 0x1c01, 0xfffffc00, 0x00000085    // inputs
        .word 0x1c10, 0xc0000000, 0x00000540    // DPI pin 2 = signal 0x15, every other pin's source low
        .word 0x1c11, 0xc0000000, 0x00000000
        .word 0x1c12, 0xff000000, 0x00000000
        .word 0x1c20, 0xc0000000, 0x00000040    // DPI pins 2 and 7 driven: pin 7, the codecs' reset, low
        .word 0x1c21, 0xc0000000, 0x00000040
        .word 0x1c22, 0xff000000, 0x00000000
.equ N_SRU, 23

.section pm 0x8c3a0
audio_init:
        R7 = 0x40000;
        L0 = 0; L4 = 0;
        M0 = 0; M1 = 1;
        I0 = AU_SRU;
        LCNTR = N_SRU, DO sru UNTIL LCE;
          R0 = DM(I0, M1);
          I4 = R0;
          R1 = DM(I0, M1);
          R2 = DM(I0, M1);
          R0 = DM(I4, M0);
          R0 = R0 AND R1;
          R0 = R0 OR R2;
sru:      DM(I4, M0) = R0;

        R0 = 0xc0200004;                    // clock A = external / 4
        DM(0x24c1) = R0;
        R0 = 0xc0800010;                    // clock B = external / 16
        DM(0x24c3) = R0;
        R0 = 0x02000080;
        DM(0x24c4) = R0;
        R0 = 0xc0000100;                    // frame sync A = external / 256
        DM(0x24c0) = R0;
        R0 = 0xc0000400;                    // frame sync B = external / 1024
        DM(0x24c2) = R0;
        R0 = 0;
        DM(0x24c6) = R0;
        DM(0x24c7) = R0;
        DM(0x24c8) = R0;
        DM(0x24c9) = R0;

        BIT SET FLAGS FLG2;                 // as the stock program has it
        R4 = 16;                            // The codecs (three CS4272, stand-alone mode) must
        CALL wait;                          // be held in reset until MCLK and LRCK are stable.
        USTAT1 = DM(0x1c11);                // Their reset is DPI pin 7: low from the routing
        BIT CLR USTAT1 0xfc0;               // above, and high from here on, which is what the
        BIT SET USTAT1 0x40;                // stock program does right after it starts the
        DM(0x1c11) = USTAT1;                // clock generators.
        RTS;

audio_ports:                                // every port on, to be read and written word by word
        R0 = RX_AB;
        DM(SPCTL0) = R0;
        R0 = RX_A;
        DM(SPCTL1) = R0;
        DM(SPCTL2) = R0;
        R0 = TX_AB;
        DM(SPCTL4) = R0;
        R0 = TX_A;
        DM(SPCTL5) = R0;
        DM(SPCTL6) = R0;
        RTS;
