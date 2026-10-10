// nam_bench.asm - run the NAM kernel on the SHARC itself and report.
//
// Assembled after nam_a2_simd.asm, which supplies nam_reset, nam_process and
// the model, and report.asm, which supplies the reporting (tools/sharc/
// nam_bench.py). Processes BN samples of a test signal
// from reset with the core timer running, compares the output with what the
// simulator computed, and reports on FLAG2 and FLAG3 (one of them is the line
// the LPC4350 reads) in frames of 18 words:
//
//     0 "NAM2"   1 core cycles for the BN samples
//     2 max |out - expected| as a float
//     3 the number of the first sample that is more than 1e-4 off, or -1
//     4-16 from a run of one sample from reset, made first:
//       4 the output      5-9 the head's input, channels 0, 1, 2, 3, 7
//       10 the head ring's next column (zero)     11 how far its pointer moved (8)
//       12, 13 layer 0's activations for channels 2 and 7
//       14 the last layer's for channel 7         15 the final x[7]
//       16 the output again, worked out here from the head's input one term
//          at a time
//     17 the output for sample 3, after three more samples
//
// The core clock is set up first, the way the stock program does it
// (tools/sharc/HARDWARE.md): 450 MHz. The cycle counts do not depend on it,
// but the length of a unit does, which is how the receiver can tell.
//
// Order of events: 16 units low; a pulse (1 unit high, 1 low); the PLL; one
// pulse per check passed (this file's data loaded, model header C = 8, its
// layer count and table pointer, weights loaded); the looks into the first samples;
// nam_reset; a pulse; nam_process; a pulse; frames for ever. A failed check ends in pulses 3 units
// high, 1 low, for ever; a program that hangs leaves the line low.

.equ BN,      32
.equ REP_WORDS, 18
.equ RND32,   0x10000           // MODE1: round floats to 32 bits
.equ CBUFEN,  0x1000000         // MODE1: circular buffers
.equ TIMEN,   0x20              // MODE2: core timer

.section dm 0xc0400
REP_UNIT: .word 0x40000
REP:  .word 0x4e414d32
        .space 17
B_IN:   .space BN               // test signal and expected output: loaded with the model
B_REF:  .space BN
B_OUT:  .space BN

.section pm 0x8c100
start:  BIT SET FLAGS OUTS;
        BIT CLR FLAGS LINE;
        BIT SET MODE1 RND32 | CBUFEN;
        NOP;
        R7 = 0x40000;
        R4 = 16;
        CALL wait;
        CALL mark;
        CALL pll;
        R0 = DM(REP);
        R1 = 0x4e414d32;
        COMP(R0, R1);
        IF NE JUMP stop;
        CALL mark;
        R0 = DM(MODEL);                     // channels
        R1 = 8;
        COMP(R0, R1);
        IF NE JUMP stop;
        CALL mark;
        R0 = DM(MODEL + 1);                 // layers
        R1 = 23;
        COMP(R0, R1);
        IF NE JUMP stop;
        R0 = DM(MODEL + 2);                 // -> layer table
        R1 = MODEL + 11;
        COMP(R0, R1);
        IF NE JUMP stop;
        CALL mark;
        I0 = DM(MODEL + 3);                 // -> rechannel weights: not all zero
        L0 = 0;
        M1 = 1;
        R0 = DM(I0, M1);
        R1 = DM(I0, M1);
        R0 = R0 OR R1;
        IF EQ JUMP stop;
        CALL mark;

        CALL nam_reset;                     // words 4 to 17: one sample from reset, looked into
        R0 = B_IN;
        DM(NAM_IN) = R0;
        R0 = B_OUT;
        DM(NAM_OUT) = R0;
        R0 = 1;
        DM(NAM_N) = R0;
        CALL nam_process;
        R0 = DM(B_OUT);
        DM(REP + 4) = R0;
        I0 = DM(MODEL + 6);                 // the head ring: its newest column is at its base
        L0 = 0;
        M1 = 1;
        M2 = 4;
        R0 = DM(I0, M1);
        DM(REP + 5) = R0;
        R0 = DM(I0, M1);
        DM(REP + 6) = R0;
        R0 = DM(I0, M1);
        DM(REP + 7) = R0;
        R0 = DM(I0, M2);
        DM(REP + 8) = R0;
        R0 = DM(I0, M1);                    // [7]
        DM(REP + 9) = R0;
        R0 = DM(I0, M1);                    // [8]: an older column, zero
        DM(REP + 10) = R0;
        R0 = DM(MODEL + 6);
        R1 = DM(MODEL + 8);
        R0 = R1 - R0;                       // the ring pointer has moved on by C
        DM(REP + 11) = R0;
        R0 = DM(AL + 2);
        DM(REP + 12) = R0;
        R0 = DM(AL + 7);
        DM(REP + 13) = R0;
        R0 = DM(AL + 183);
        DM(REP + 14) = R0;
        R0 = DM(X + 7);
        DM(REP + 15) = R0;
        I0 = DM(MODEL + 6);                 // the head over the newest column, one term at a time
        R0 = DM(MODEL + 4);
        R1 = 120;
        R0 = R0 + R1;
        I8 = R0;
        L8 = 0;
        M9 = 1;
        R8 = 0;
        LCNTR = 8, DO head1 UNTIL LCE;
          F0 = DM(I0, M1), F4 = PM(I8, M9);
          F12 = F0 * F4;
head1:    F8 = F8 + F12;
        F0 = DM(MODEL + 10);
        F8 = F8 + F0;
        F4 = DM(MODEL + 5);
        F8 = F8 * F4;
        DM(REP + 16) = F8;
        R0 = B_IN + 1;
        DM(NAM_IN) = R0;
        R0 = 3;
        DM(NAM_N) = R0;
        CALL nam_process;
        R0 = DM(B_OUT + 2);
        DM(REP + 17) = R0;

        CALL nam_reset;
        CALL mark;
        R0 = B_IN;
        DM(NAM_IN) = R0;
        R0 = B_OUT;
        DM(NAM_OUT) = R0;
        R0 = BN;
        DM(NAM_N) = R0;
        R0 = 0xffffffff;
        TPERIOD = R0;
        TCOUNT = R0;
        BIT SET MODE2 TIMEN;
        CALL nam_process;
        BIT CLR MODE2 TIMEN;
        NOP;
        R0 = 0xffffffff;
        R1 = TCOUNT;
        R0 = R0 - R1;
        DM(REP + 1) = R0;
        CALL mark;

        I0 = B_OUT;                         // nam_process leaves L0 and L1 zero
        I1 = B_REF;
        M1 = 1;
        R8 = 0;
        R2 = 0;                             // sample number
        R9 = -1;                            // the first one that is off
        F10 = 0.0001;
diff:   F0 = DM(I0, M1);
        F1 = DM(I1, M1);
        F0 = F0 - F1;
        F0 = ABS F0;
        F8 = MAX(F8, F0);
        COMP(F0, F10);
        IF LE JUMP same;
        R9 = PASS R9;
        IF LT R9 = PASS R2;
same:   R2 = R2 + 1;
        R0 = BN;
        COMP(R2, R0);
        IF LT JUMP diff;
        DM(REP + 3) = R9;
        DM(REP + 2) = F8;

send:   CALL frame;
        JUMP send;
