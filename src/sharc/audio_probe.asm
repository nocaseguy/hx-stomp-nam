// audio_probe.asm - are the audio clocks running, and which inputs are alive?
//
// Assembled with report.asm and audio.asm (tools/sharc/mkreport.py). Sets up
// the core clock and the audio hardware (audio.asm) and then for ever: counts the words each one delivers in A_WIN
// core cycles, notes the largest magnitude, and sends a frame of 10 words:
//
//     0 "AUD1"
//     1-4 words received: SPORT0 A, SPORT0 B, SPORT1 A, SPORT2 A
//     5-8 the largest |word| on each, in the same order
//     9 the core cycles the count took
//
// It also feeds the four transmitting ports a quiet square wave while it
// counts (one period every 256 words: 375 Hz on a 48 kHz stereo port, 1.5 kHz
// on the 192 kHz one): if that can be heard, the output converters are alive
// whatever the inputs do. It counts A_REPS times over, a good half second,
// before each frame and reports the last count, so that the report line
// (FLAG3) has been still and low for that long when that count is made: the
// stock program pulls FLAG3 low when it is up and leaves it there, and the
// board may use it for more than telling the LPC4350. Transmit data registers, also from memory: control + 0x60 (A), + 0x62 (B).
//
// A stereo port delivers two words a frame, so at 450 MHz the sample rate is
// words / 2 * 450e6 / cycles. On the unit: 48.0 kHz on the first three ports,
// 192 kHz on SPORT2 A, and the tone is heard on the main outputs, both sends
// and the headphones.

.equ REP_WORDS, 10
.equ TIMEN,   0x20              // MODE2: core timer
.equ LEVEL,   0x02000000        // 36 dB under full scale

.section dm 0xc0400
REP_UNIT: .word 0x40000
REP:    .word 0x41554431
        .space 9
A_WIN:  .word 0x04000000        // about 150 ms at 450 MHz
A_REPS: .word 4                 // counts made between frames; the last one is reported
A_LEFT: .space 1
A_TX:   .space 4                // words sent so far: SPORT4 A, SPORT4 B, SPORT5 A, SPORT6 A

.section pm 0x8c100
start:  BIT SET FLAGS OUTS;
        BIT CLR FLAGS LINE;
        R7 = 0x40000;
        R4 = 16;
        CALL wait;
        CALL mark;
        CALL pll;
        CALL mark;

        CALL audio_init;
        CALL audio_ports;
        CALL mark;

again:  R0 = DM(A_REPS);
        DM(A_LEFT) = R0;
count:  R8 = 0; R9 = 0; R10 = 0; R11 = 0;
        R12 = 0; R13 = 0; R14 = 0; R15 = 0;
        R3 = 0xc0000000;
        R4 = 0x18000000;
        R5 = DM(A_WIN);
        R6 = 0xffffffff;
        R7 = LEVEL;
        TPERIOD = R6;
        TCOUNT = R6;
        BIT SET MODE2 TIMEN;
poll:   R0 = DM(SPCTL0);
        R1 = R0 AND R3;
        IF EQ JUMP p0b;
        R2 = DM(RXSP0A);
        R8 = R8 + 1;
        R2 = ABS R2;
        R12 = MAX(R12, R2);
p0b:    R1 = R0 AND R4;
        IF EQ JUMP p1;
        R2 = DM(RXSP0B);
        R9 = R9 + 1;
        R2 = ABS R2;
        R13 = MAX(R13, R2);
p1:     R0 = DM(SPCTL1);
        R1 = R0 AND R3;
        IF EQ JUMP p2;
        R2 = DM(RXSP1A);
        R10 = R10 + 1;
        R2 = ABS R2;
        R14 = MAX(R14, R2);
p2:     R0 = DM(SPCTL2);
        R1 = R0 AND R3;
        IF EQ JUMP t4a;
        R2 = DM(RXSP2A);
        R11 = R11 + 1;
        R2 = ABS R2;
        R15 = MAX(R15, R2);
t4a:    R0 = DM(SPCTL4);
        R1 = R0 AND R3;
        COMP(R1, R3);
        IF EQ JUMP t4b;                    // full
        R2 = DM(A_TX + 0);
        R2 = R2 + 1;
        DM(A_TX + 0) = R2;
        R1 = LSHIFT R2 BY 24;               // bit 7 of the count is the sign
        R1 = ASHIFT R1 BY -31;
        R1 = R1 XOR R7;
        DM(TXSP4A) = R1;
t4b:    R0 = DM(SPCTL4);
        R1 = R0 AND R4;
        COMP(R1, R4);
        IF EQ JUMP t5;                    // full
        R2 = DM(A_TX + 1);
        R2 = R2 + 1;
        DM(A_TX + 1) = R2;
        R1 = LSHIFT R2 BY 24;               // bit 7 of the count is the sign
        R1 = ASHIFT R1 BY -31;
        R1 = R1 XOR R7;
        DM(TXSP4B) = R1;
t5:     R0 = DM(SPCTL5);
        R1 = R0 AND R3;
        COMP(R1, R3);
        IF EQ JUMP t6;                    // full
        R2 = DM(A_TX + 2);
        R2 = R2 + 1;
        DM(A_TX + 2) = R2;
        R1 = LSHIFT R2 BY 24;               // bit 7 of the count is the sign
        R1 = ASHIFT R1 BY -31;
        R1 = R1 XOR R7;
        DM(TXSP5A) = R1;
t6:     R0 = DM(SPCTL6);
        R1 = R0 AND R3;
        COMP(R1, R3);
        IF EQ JUMP p3;                    // full
        R2 = DM(A_TX + 3);
        R2 = R2 + 1;
        DM(A_TX + 3) = R2;
        R1 = LSHIFT R2 BY 24;               // bit 7 of the count is the sign
        R1 = ASHIFT R1 BY -31;
        R1 = R1 XOR R7;
        DM(TXSP6A) = R1;
p3:     R1 = TCOUNT;
        R0 = R6 - R1;
        COMP(R0, R5);
        IF LT JUMP poll;
        BIT CLR MODE2 TIMEN;
        R1 = DM(A_LEFT);
        R1 = R1 - 1;
        DM(A_LEFT) = R1;
        IF NE JUMP count;
        DM(REP + 1) = R8;
        DM(REP + 2) = R9;
        DM(REP + 3) = R10;
        DM(REP + 4) = R11;
        DM(REP + 5) = R12;
        DM(REP + 6) = R13;
        DM(REP + 7) = R14;
        DM(REP + 8) = R15;
        DM(REP + 9) = R0;
        CALL frame;
        JUMP again;
