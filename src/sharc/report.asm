// report.asm - how our test programs on the SHARC tell the LPC4350 what they
// found, plus the core clock set-up. Assembled together with the program that
// uses it (tools/sharc/mkboot.py takes several sources), which defines:
//
//     REP        the words to send, the first a magic number
//     REP_WORDS  how many (.equ)
//     REP_UNIT   a word holding the length of a unit in core cycles (0x40000;
//                the simulator runs with fewer)
//
// frame sends REP once on FLAG3, which GPIO7[0] of the LPC4350 is taken to
// follow. (Up to here everything drove FLAG2 and FLAG3 together; the stock
// program holds both high and moves only FLAG3, so FLAG2 probably does
// something else on the board and is now left alone.) Line code: a frame starts with 8 units high and 1 low; then
// every bit, most significant first, is 3 units high for a one or 1 unit high
// for a zero, followed by 1 unit low. The receiver only compares each high
// time with the low time after it, so neither side needs to know the other's
// clock (src/smoke_frames.h).
//
// mark gives one pulse, 1 unit high and 1 low, of 0x40000 cycles whatever
// REP_UNIT says; stop never returns and gives pulses 3 units high, 1 low.
// pll takes the core from 400 to 450 MHz with the stock program's values
// (tools/sharc/HARDWARE.md). The program must make FLAG2 and FLAG3 outputs
// (BIT SET FLAGS OUTS) before using any of this. Registers R0-R7, I0, L0, M1
// and USTAT1 are used.

.equ LINE,    0x40              // FLAG3: the line the LPC4350 reads (to be confirmed)
.equ FLG2,    0x10              // FLAG2: left to the program
.equ OUTS,    0xa0              // their output enables
.equ PMCTL,   0x2000

.section pm 0x8c300
frame:  R0 = 0x40000;                       // the frame, once
        R7 = DM(REP_UNIT);
        R7 = PASS R7;
        IF LE R7 = PASS R0;                 // never a unit of zero: that would be 2^32 passes
        R5 = 1;
        R6 = 3;
        L0 = 0;
        M1 = 1;
        BIT SET FLAGS LINE;
        R4 = 8;
        CALL wait;
        BIT CLR FLAGS LINE;
        R4 = PASS R5;
        CALL wait;
        I0 = REP;
        R2 = REP_WORDS;
word:   R1 = DM(I0, M1);
        R3 = 32;
bit:    R4 = PASS R5;
        R1 = PASS R1;
        IF LT R4 = PASS R6;
        BIT SET FLAGS LINE;
        CALL wait;
        BIT CLR FLAGS LINE;
        R4 = PASS R5;
        CALL wait;
        R1 = LSHIFT R1 BY 1;
        R3 = R3 - 1;
        IF NE JUMP bit;
        R2 = R2 - 1;
        IF NE JUMP word;
        RTS;

pll:    R0 = 8;                             // the stock program's values, in its order
        DM(0x4c20) = R0;
        R0 = 0;
        DM(0x4c00) = R0;
        USTAT1 = DM(PMCTL);
        BIT CLR USTAT1 0xc0;
        BIT SET USTAT1 0x240;
        DM(PMCTL) = USTAT1;
        R0 = 64;
        CALL pause;
        USTAT1 = DM(PMCTL);
        BIT CLR USTAT1 0x200;
        BIT SET USTAT1 0x8100;              // bypass
        DM(PMCTL) = USTAT1;
        R0 = 0x4000;
        CALL pause;
        USTAT1 = DM(PMCTL);
        BIT CLR USTAT1 0x8000;
        DM(PMCTL) = USTAT1;
        R0 = 64;
        CALL pause;
        USTAT1 = DM(PMCTL);
        BIT CLR USTAT1 0x13f;
        BIT SET USTAT1 0x8012;              // multiplier 18, bypass
        DM(PMCTL) = USTAT1;
        R0 = 0x4000;
        CALL pause;
        USTAT1 = DM(PMCTL);
        BIT CLR USTAT1 0x8000;
        DM(PMCTL) = USTAT1;
        R0 = 64;
        CALL pause;
        USTAT1 = DM(PMCTL);
        BIT CLR USTAT1 0x7c00c0;
        BIT SET USTAT1 0x400200;
        DM(PMCTL) = USTAT1;
        R0 = 64;
pause:  LCNTR = R0, DO idle UNTIL LCE;
idle:     NOP;
        RTS;

stop:   R7 = 0x40000;
        R4 = 3;
        BIT SET FLAGS LINE;
        CALL wait;
        R4 = 1;
        BIT CLR FLAGS LINE;
        CALL wait;
        JUMP stop;

mark:   R7 = 0x40000;                       // one unit high, one low; no data needed
        R4 = 1;
        BIT SET FLAGS LINE;
        CALL wait;
        R4 = 1;
        BIT CLR FLAGS LINE;
wait:   LCNTR = R7, DO unit UNTIL LCE;      // R4 units
unit:     NOP;
        R4 = R4 - 1;
        IF NE JUMP wait;
        RTS;
