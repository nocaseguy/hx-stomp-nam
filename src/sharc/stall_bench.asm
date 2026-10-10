// stall_bench.asm - what the processor's pipeline costs: short pieces of code of
// the kinds nam_a2_simd.asm is made of, each run REPS times with the core timer
// counting. The simulator counts one for every instruction, the processor one
// for every cycle, so the difference between what the unit reports and what
// tools/sharc/mkreport.py prints is the cycles lost, REPS times over.
// Assembled after report.asm; shown by src/smoke_words.c (`make smoke-stall`).
//
// Frame: "STAL", then a count for each case below. Every case is its piece
// between `LCNTR = REPS, DO .. UNTIL LCE` and a closing NOP, called and
// returned from; case 0 measures that frame with eight NOPs inside.
//    0  eight NOPs (8 instructions a pass)
//    1  a loop of 48 NOPs, count in the instruction (49 instructions a pass)
//    2  the same, count from a register (49 instructions a pass)
//    3  pointers set, 3 NOPs, then 48 multiply-adds reading DM and PM (55 instructions a pass)
//    4  the same with 8 multiply-adds (15 instructions a pass)
//    5  an index register copied and used at once, four times (9 instructions a pass)
//    6  the same with one NOP between (13 instructions a pass)
//    7  eight single loads through PM (9 instructions a pass)
//    8  an index register loaded from memory and used at once, four times (9 instructions a pass)
//    9  48 multiply-adds, DM and PM in the same memory block (55 instructions a pass)
//   10  a store through DM and PM in one instruction, eight times (9 instructions a pass)
//   11  two loops of 8 one after the other (19 instructions a pass)
//   12  the 1x1 of nam_a2_simd.asm for one pair of channels: both elements, broadcast loads from a ring of 9 in block 2, weights from a ring in block 3 (12 instructions a pass)
//   13  the same with one element (12 instructions a pass)
//   14  the same as the first, inputs from a ring of 9 in block 1 (12 instructions a pass)
//   15  the same as the first, inputs not a ring (block 1, 9000 words on end) (12 instructions a pass)
//   16  the same as the first, weights not a ring (block 3, 18000 words on end) (12 instructions a pass)
//   17  the same as the first, both elements but loads not broadcast (12 instructions a pass)
//   18  2 loads through PM, each 16 addresses after the one before (18 instructions a pass)
//   19  3 loads through PM, each 16 addresses after the one before (34 instructions a pass)
//   20  2 loads through PM, each 32 addresses after the one before (34 instructions a pass)
//   21  3 loads through PM, each 32 addresses after the one before (66 instructions a pass)
//   22  2 loads through PM, each 8 addresses after the one before (10 instructions a pass)
//   23  3 loads through PM, each 8 addresses after the one before (18 instructions a pass)
//   24  4 loads through PM, each 8 addresses after the one before (26 instructions a pass)
//   25  5 loads through PM, each 8 addresses after the one before (34 instructions a pass)
//   26  3 loads through PM, each 4 addresses after the one before (10 instructions a pass)
//   27  5 loads through PM, each 4 addresses after the one before (18 instructions a pass)
//   28  9 loads through PM, each 2 addresses after the one before (18 instructions a pass)

.equ REPS,    200
.equ REP_WORDS, 30
.equ RND32,   0x10000           // MODE1: round floats to 32 bits
.equ TIMEN,   0x20              // MODE2: core timer
.equ S_PM,    0xe0000           // block 3: data reached through PM
.equ S_B1,    0xb2000           // block 1
.equ PEYEN,   0x200000          // MODE1: second processing element on
.equ BDCST1,  0x800000          // MODE1: loads through I1 go to both elements
.equ CBUFEN,  0x1000000         // MODE1: circular buffers

.section dm 0xc0400
REP_UNIT: .word 0x40000
REP:    .word 0x5354414c
        .space 29
S_PTR:  .word S_DM              // a pointer to load an index register from
S_DM:   .space 64

.section pm 0x8c400
start:  BIT SET FLAGS OUTS;
        BIT CLR FLAGS LINE;
        BIT SET MODE1 RND32 | CBUFEN;
        NOP;
        R7 = 0x40000;
        R4 = 16;
        CALL wait;
        CALL mark;
        CALL pll;
        L0 = 0; L1 = 0; L2 = 0; L3 = 0; L4 = 0; L8 = 0; L13 = 0;
        M0 = 0; M1 = 1; M8 = 0; M9 = 1;
        I2 = S_DM;
        I4 = S_PTR;
        R10 = 8;
        R11 = 48;
        R0 = 0xffffffff;
        TPERIOD = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c0;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 1) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c1;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 2) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c2;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 3) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c3;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 4) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c4;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 5) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c5;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 6) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c6;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 7) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c7;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 8) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c8;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 9) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c9;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 10) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c10;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 11) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c11;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 12) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c12;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 13) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c13;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 14) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c14;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 15) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c15;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 16) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c16;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 17) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c17;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 18) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c18;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 19) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c19;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 20) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c20;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 21) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c21;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 22) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c22;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 23) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c23;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 24) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c24;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 25) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c25;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 26) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c26;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 27) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c27;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 28) = R0;
        R0 = 0xffffffff;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL c28;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 29) = R0;
again:  CALL frame;
        JUMP again;

c0:     LCNTR = REPS, DO e0 UNTIL LCE;      // eight NOPs
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
e0:       NOP;
        RTS;

c1:     LCNTR = REPS, DO e1 UNTIL LCE;      // a loop of 48 NOPs, count in the instruction
          LCNTR = 48, DO i1 UNTIL LCE;
i1:         NOP;
e1:       NOP;
        RTS;

c2:     LCNTR = REPS, DO e2 UNTIL LCE;      // the same, count from a register
          LCNTR = R11, DO i2 UNTIL LCE;
i2:         NOP;
e2:       NOP;
        RTS;

c3:     LCNTR = REPS, DO e3 UNTIL LCE;      // pointers set, 3 NOPs, then 48 multiply-adds reading DM and PM
          I1 = S_DM;
          I8 = S_PM;
          NOP; NOP; NOP;
          LCNTR = R11, DO i3 UNTIL LCE;
i3:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M9);
e3:       NOP;
        RTS;

c4:     LCNTR = REPS, DO e4 UNTIL LCE;      // the same with 8 multiply-adds
          I1 = S_DM;
          I8 = S_PM;
          NOP; NOP; NOP;
          LCNTR = R10, DO i4 UNTIL LCE;
i4:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M9);
e4:       NOP;
        RTS;

c5:     LCNTR = REPS, DO e5 UNTIL LCE;      // an index register copied and used at once, four times
          I1 = I2;
          F0 = DM(I1, M0);
          I1 = I2;
          F0 = DM(I1, M0);
          I1 = I2;
          F0 = DM(I1, M0);
          I1 = I2;
          F0 = DM(I1, M0);
e5:       NOP;
        RTS;

c6:     LCNTR = REPS, DO e6 UNTIL LCE;      // the same with one NOP between
          I1 = I2;
          NOP;
          F0 = DM(I1, M0);
          I1 = I2;
          NOP;
          F0 = DM(I1, M0);
          I1 = I2;
          NOP;
          F0 = DM(I1, M0);
          I1 = I2;
          NOP;
          F0 = DM(I1, M0);
e6:       NOP;
        RTS;

c7:     LCNTR = REPS, DO e7 UNTIL LCE;      // eight single loads through PM
          I8 = S_PM;
          F4 = PM(I8, M9);
          F4 = PM(I8, M9);
          F4 = PM(I8, M9);
          F4 = PM(I8, M9);
          F4 = PM(I8, M9);
          F4 = PM(I8, M9);
          F4 = PM(I8, M9);
e7:       NOP;
        RTS;

c8:     LCNTR = REPS, DO e8 UNTIL LCE;      // an index register loaded from memory and used at once, four times
          I3 = DM(M0, I4);
          F0 = DM(I3, M0);
          I3 = DM(M0, I4);
          F0 = DM(I3, M0);
          I3 = DM(M0, I4);
          F0 = DM(I3, M0);
          I3 = DM(M0, I4);
          F0 = DM(I3, M0);
e8:       NOP;
        RTS;

c9:     LCNTR = REPS, DO e9 UNTIL LCE;      // 48 multiply-adds, DM and PM in the same memory block
          I1 = S_PM + 64;
          I8 = S_PM;
          NOP; NOP; NOP;
          LCNTR = R11, DO i9 UNTIL LCE;
i9:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M9);
e9:       NOP;
        RTS;

c10:     LCNTR = REPS, DO e10 UNTIL LCE;      // a store through DM and PM in one instruction, eight times
          I0 = S_DM;
          I13 = S_PM;
          NOP; NOP; NOP;
          DM(I0, M1) = F8, PM(I13, M9) = F8;
          DM(I0, M1) = F8, PM(I13, M9) = F8;
          DM(I0, M1) = F8, PM(I13, M9) = F8;
e10:       NOP;
        RTS;

c11:     LCNTR = REPS, DO e11 UNTIL LCE;      // two loops of 8 one after the other
          LCNTR = R10, DO i11 UNTIL LCE;
i11:        NOP;
          LCNTR = R10, DO i12 UNTIL LCE;
i12:        NOP;
e11:       NOP;
        RTS;

c12:    B1 = S_DM;                         // the 1x1 of nam_a2_simd.asm for one pair of channels: both elements, broadcast loads from a ring of 9 in block 2, weights from a ring in block 3
        L1 = 9;
        B8 = S_PM;
        L8 = 18;
        M5 = 8;
        M11 = 2;
        BIT SET MODE1 PEYEN | BDCST1;
        NOP;
        LCNTR = REPS, DO e12 UNTIL LCE;
          R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
          LCNTR = M5, DO i112 UNTIL LCE;
i112:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
          F12 = F0 * F4, F8 = F8 + F12;
          F8 = F8 + F12;
e12:      NOP;
        BIT CLR MODE1 PEYEN | BDCST1;
        NOP;
        L1 = 0;
        L8 = 0;
        RTS;

c13:    B1 = S_DM;                         // the same with one element
        L1 = 9;
        B8 = S_PM;
        L8 = 18;
        M5 = 8;
        M11 = 2;
        LCNTR = REPS, DO e13 UNTIL LCE;
          R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
          LCNTR = M5, DO i113 UNTIL LCE;
i113:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
          F12 = F0 * F4, F8 = F8 + F12;
          F8 = F8 + F12;
e13:      NOP;
        L1 = 0;
        L8 = 0;
        RTS;

c14:    B1 = S_B1;                         // the same as the first, inputs from a ring of 9 in block 1
        L1 = 9;
        B8 = S_PM;
        L8 = 18;
        M5 = 8;
        M11 = 2;
        BIT SET MODE1 PEYEN | BDCST1;
        NOP;
        LCNTR = REPS, DO e14 UNTIL LCE;
          R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
          LCNTR = M5, DO i114 UNTIL LCE;
i114:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
          F12 = F0 * F4, F8 = F8 + F12;
          F8 = F8 + F12;
e14:      NOP;
        BIT CLR MODE1 PEYEN | BDCST1;
        NOP;
        L1 = 0;
        L8 = 0;
        RTS;

c15:    B1 = S_B1;                         // the same as the first, inputs not a ring (block 1, 9000 words on end)
        L1 = 0;
        B8 = S_PM;
        L8 = 18;
        M5 = 8;
        M11 = 2;
        BIT SET MODE1 PEYEN | BDCST1;
        NOP;
        LCNTR = REPS, DO e15 UNTIL LCE;
          R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
          LCNTR = M5, DO i115 UNTIL LCE;
i115:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
          F12 = F0 * F4, F8 = F8 + F12;
          F8 = F8 + F12;
e15:      NOP;
        BIT CLR MODE1 PEYEN | BDCST1;
        NOP;
        L1 = 0;
        L8 = 0;
        RTS;

c16:    B1 = S_DM;                         // the same as the first, weights not a ring (block 3, 18000 words on end)
        L1 = 9;
        B8 = S_PM;
        L8 = 0;
        M5 = 8;
        M11 = 2;
        BIT SET MODE1 PEYEN | BDCST1;
        NOP;
        LCNTR = REPS, DO e16 UNTIL LCE;
          R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
          LCNTR = M5, DO i116 UNTIL LCE;
i116:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
          F12 = F0 * F4, F8 = F8 + F12;
          F8 = F8 + F12;
e16:      NOP;
        BIT CLR MODE1 PEYEN | BDCST1;
        NOP;
        L1 = 0;
        L8 = 0;
        RTS;

c17:    B1 = S_DM;                         // the same as the first, both elements but loads not broadcast
        L1 = 9;
        B8 = S_PM;
        L8 = 18;
        M5 = 8;
        M11 = 2;
        BIT SET MODE1 PEYEN;
        NOP;
        LCNTR = REPS, DO e17 UNTIL LCE;
          R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
          LCNTR = M5, DO i117 UNTIL LCE;
i117:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
          F12 = F0 * F4, F8 = F8 + F12;
          F8 = F8 + F12;
e17:      NOP;
        BIT CLR MODE1 PEYEN;
        NOP;
        L1 = 0;
        L8 = 0;
        RTS;

c18:    I8 = S_PM;                          // 2 loads through PM, each 16 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e18 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e18:      NOP;
        RTS;

c19:    I8 = S_PM;                          // 3 loads through PM, each 16 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e19 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e19:      NOP;
        RTS;

c20:    I8 = S_PM;                          // 2 loads through PM, each 32 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e20 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e20:      NOP;
        RTS;

c21:    I8 = S_PM;                          // 3 loads through PM, each 32 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e21 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e21:      NOP;
        RTS;

c22:    I8 = S_PM;                          // 2 loads through PM, each 8 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e22 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e22:      NOP;
        RTS;

c23:    I8 = S_PM;                          // 3 loads through PM, each 8 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e23 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e23:      NOP;
        RTS;

c24:    I8 = S_PM;                          // 4 loads through PM, each 8 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e24 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e24:      NOP;
        RTS;

c25:    I8 = S_PM;                          // 5 loads through PM, each 8 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e25 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP; NOP; NOP; NOP; NOP;
          F4 = PM(I8, M8);
e25:      NOP;
        RTS;

c26:    I8 = S_PM;                          // 3 loads through PM, each 4 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e26 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP;
          F4 = PM(I8, M8);
e26:      NOP;
        RTS;

c27:    I8 = S_PM;                          // 5 loads through PM, each 4 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e27 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP;
          F4 = PM(I8, M8);
          NOP; NOP; NOP;
          F4 = PM(I8, M8);
e27:      NOP;
        RTS;

c28:    I8 = S_PM;                          // 9 loads through PM, each 2 addresses after the one before
        NOP; NOP; NOP;
        LCNTR = REPS, DO e28 UNTIL LCE;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
          NOP;
          F4 = PM(I8, M8);
e28:      NOP;
        RTS;

