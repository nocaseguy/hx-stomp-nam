// nam_live.asm - the NAM model in the audio path: guitar in, modelled amp out.
//
// Assembled after nam_a2_simd.asm, report.asm and audio.asm (tools/sharc/
// nam_live.py). The 48 kHz serial ports run on chained DMA, each into or out
// of a ring of LN words of its own, the way the stock program runs them: a
// transfer control block is four words {next block's last word & 0x7ffff,
// count, step, start}, and one that points at itself goes round for ever.
// The core only follows the receiver's DMA pointer. For every frame that has
// come in, live_tick adds the six input words (left and right of the three
// receiving ports: which jack is where does not have to be known), runs
// nam_process on that one sample, and writes the result to both channels of
// the three transmitting ports, LAHEAD words ahead of where the transmitter
// is reading. Around the model there is a short chain of effects (fx_gate,
// fx_post below):
//     inputs added -> gate -> input gain -> model -> three equaliser bands
//     -> delay -> output level -> clamp just short of full scale
// (src/pedal.c does not use the gate: it leaves it open.)
// With the parameters as they are loaded the gate stays open, the bands pass
// the signal unchanged and the delay adds nothing. The 192 kHz ports are left
// off.
//
// The report line is high while live_tick works on a sample and low while
// the program waits for the next, so the share of the time it is high is the
// share of the processor in use; the LPC4350 samples it (src/pedal.c). A
// frame's units are hundreds of times longer than those pulses.
//
// Before it starts it sends three frames on the report line: "LIVE", the
// distance between the transmit and receive rings it measured, and the
// parameters.
//
// Parameters come from the LPC4350 over the SPI port the program was booted
// through, two words a message, received by DMA the way the boot kernel
// receives (src/smoke_sharc.c sends them): a number 0..31 and a value.
//     0  input gain (float)            1  model: 0 = off, the input goes straight on
//     2  output level (float)          3  send a frame with the parameters now
//                                         (the sound stops for a moment); if
//                                         the value is not 0 it is an address,
//                                         and the word there is sent in place
//                                         of the level
//     4  gate threshold: the gate opens while the input's envelope is above
//        it (float, full scale 1.0; below 0 it is always open)
//     5, 6  how fast the gate opens and closes: the share of the remaining
//        way its gain moves each sample
//     7  what is left of the envelope after one sample
//     8-12, 13-17, 18-22  three equaliser bands, each b0, b1, b2, -a1, -a2 of
//        y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2. These only wait here:
//     26 puts the five numbers of band 0, 1 or 2 to use at once. (Used one at
//        a time as they arrive, a half-changed band can be unstable, and what
//        it does in the milliseconds until the rest arrives never leaves it.)
//     23 delay time in samples, 1..D_MAX    24 feedback    25 how much of the
//        delayed signal is added
// The frame's second word has the transmit-receive distance in its low half
// and, in its high half, the most words the receiver was ever ahead of the
// program since the last frame: 2 is keeping up, near LN is too slow.

.equ REP_WORDS, 5
.equ IISPI,   0x1080            // SPI DMA address; the boot kernel left the step at 1
.equ CSPI,    0x1082            // SPI DMA word count; writing it starts a transfer
.equ SPIDMAC, 0x1084            // bit 15: DMA transfer in progress
.equ RND32,   0x10000           // MODE1: round floats to 32 bits
.equ CBUFEN,  0x1000000         // MODE1: circular buffers
.equ LN,      64                // words in a ring: 32 frames
.equ LAHEAD,  8                 // four frames ahead of the transmitter
.equ IISP0A,  0xc40             // DMA pointers
.equ IISP4A,  0x840
.equ CPSP0A,  0xc43             // chain pointers
.equ CPSP0B,  0xc47
.equ CPSP1A,  0xc4b
.equ CPSP4A,  0x843
.equ CPSP4B,  0x847
.equ CPSP6A,  0x4843
.equ D_BUF,   0xba000           // the delay's memory: block 1 above the model's rings
.equ D_MAX,   16000             // a third of a second

.section dm 0xc0400
REP_UNIT: .word 0x40000
REP:    .word 0x4c495645
        .space 4
L_PAR:  .float 1.0              // 0: input gain
        .word 1                 // 1: model on
        .float 0.25             // 2: output level: 12 dB down
        .word 0                 // 3: (a request, not a value)
        .float -1.0, 0.01, 0.0005, 0.9995       // 4-7: gate, open
        .float 1.0, 0.0, 0.0, 0.0, 0.0          // 8-22: three bands, flat
        .float 1.0, 0.0, 0.0, 0.0, 0.0
        .float 1.0, 0.0, 0.0, 0.0, 0.0
        .word 12000             // 23: delay, a quarter of a second
        .float 0.0, 0.0         // 24, 25: no feedback, none of it added
        .space 6
L_MSG:  .space 2                // a message: parameter number, value
L_RP:   .word 0                 // next frame to process: an even offset into the receive rings
L_SKEW: .word 0                 // offset of the place to write from the place read
L_IN:   .space 1
L_OUT:  .space 1
L_BACK: .word 0                 // most words waiting, since the last frame
G_ENV:  .word 0                 // gate: envelope, gain
G_GAIN: .word 0
EQ_S:   .space 6                // two words of state a band
D_WP:   .word 0                 // delay: where the next sample goes
EQ_C:   .float 1.0, 0.0, 0.0, 0.0, 0.0          // the bands in use
        .float 1.0, 0.0, 0.0, 0.0, 0.0
        .float 1.0, 0.0, 0.0, 0.0, 0.0
        .space 5                // (a fourth, for a wrong band number to land in)

.section dm 0xc0700
L_RX:   .space 3*LN             // SPORT0 A, SPORT0 B, SPORT1 A
L_TX:   .space 3*LN             // SPORT4 A, SPORT4 B, SPORT6 A
L_TCB:  .word L_TCB + 3,  LN, 1, L_RX
        .word L_TCB + 7,  LN, 1, L_RX + LN
        .word L_TCB + 11, LN, 1, L_RX + 2*LN
        .word L_TCB + 15, LN, 1, L_TX
        .word L_TCB + 19, LN, 1, L_TX + LN
        .word L_TCB + 23, LN, 1, L_TX + 2*LN

.section pm 0x8c100
start:  BIT SET FLAGS OUTS;
        BIT CLR FLAGS LINE;
        BIT SET MODE1 RND32 | CBUFEN;
        R7 = 0x40000;
        R4 = 16;
        CALL wait;
        CALL mark;
        CALL pll;
        CALL nam_reset;
        CALL fx_reset;
        R0 = L_IN;
        DM(NAM_IN) = R0;
        R0 = L_OUT;
        DM(NAM_OUT) = R0;
        R0 = 1;
        DM(NAM_N) = R0;
        CALL audio_init;

        R1 = 0x7ffff;                       // chain pointers: the blocks' last words
        R0 = L_TCB + 3;   R0 = R0 AND R1;   DM(CPSP0A) = R0;
        R0 = L_TCB + 7;   R0 = R0 AND R1;   DM(CPSP0B) = R0;
        R0 = L_TCB + 11;  R0 = R0 AND R1;   DM(CPSP1A) = R0;
        R0 = L_TCB + 15;  R0 = R0 AND R1;   DM(CPSP4A) = R0;
        R0 = L_TCB + 19;  R0 = R0 AND R1;   DM(CPSP4B) = R0;
        R0 = L_TCB + 23;  R0 = R0 AND R1;   DM(CPSP6A) = R0;
        R0 = 0x013d29f1;                    // the stock control words: I2S, 32 bits, DMA, chained
        DM(SPCTL0) = R0;
        R0 = 0x000d29f1;
        DM(SPCTL1) = R0;
        R0 = 0x033d29f1;
        DM(SPCTL4) = R0;
        R0 = 0x020d29f1;
        DM(SPCTL6) = R0;

        R7 = 0x40000;                       // let both directions get going, then see how far
        R4 = 4;                             // apart their pointers are
        CALL wait;
        R0 = DM(IISP4A);
        R1 = DM(IISP0A);
        R0 = R0 - R1;                       // tx - rx
        R2 = LAHEAD - (L_TX - L_RX);
        R0 = R0 + R2;                       // (tx - L_TX) - (rx - L_RX) + LAHEAD
        R1 = LN - 2;
        R0 = R0 AND R1;                     // even, inside the ring
        DM(L_SKEW) = R0;
        DM(REP + 1) = R0;
        CALL show;
        CALL show;
        CALL arm;

live:   USTAT1 = DM(SPIDMAC);               // a message from the LPC4350?
        BIT TST USTAT1 0x8000;
        IF TF JUMP frames;
        R0 = DM(L_MSG);
        R1 = 31;
        R0 = R0 AND R1;
        R1 = 3;
        R2 = L_PAR;
        R2 = R2 + R0;
        I4 = R2;
        L4 = 0;
        M0 = 0;
        R2 = DM(L_MSG + 1);
        DM(I4, M0) = R2;
        R3 = 26;
        COMP(R0, R3);
        IF EQ CALL commit;
        COMP(R0, R1);
        IF EQ CALL show;
        CALL arm;
frames: R0 = DM(IISP0A);                    // has the receiver finished another frame?
        R1 = L_RX;
        R0 = R0 - R1;
        R1 = DM(L_RP);
        R0 = R0 - R1;
        R1 = LN - 1;
        R0 = R0 AND R1;
        R1 = 2;
        COMP(R0, R1);
        IF LT JUMP live;
        R1 = DM(L_BACK);
        R1 = MAX(R1, R0);
        DM(L_BACK) = R1;
        CALL live_tick;
        JUMP live;

commit: R3 = 3;                             // band R2's numbers from L_PAR to EQ_C
        R2 = R2 AND R3;
        R3 = R2 + R2;
        R3 = R3 + R3;
        R3 = R3 + R2;
        R4 = L_PAR + 8;
        R4 = R4 + R3;
        I4 = R4;
        R4 = EQ_C;
        R4 = R4 + R3;
        I5 = R4;
        L4 = 0;
        L5 = 0;
        M1 = 1;
        R4 = DM(I4, M1);
        DM(I5, M1) = R4;
        R4 = DM(I4, M1);
        DM(I5, M1) = R4;
        R4 = DM(I4, M1);
        DM(I5, M1) = R4;
        R4 = DM(I4, M1);
        DM(I5, M1) = R4;
        R4 = DM(I4, M1);
        DM(I5, M1) = R4;
        RTS;

arm:    R0 = L_MSG;                         // receive the next message
        DM(IISPI) = R0;
        R0 = 2;
        DM(CSPI) = R0;
        RTS;

show:   R0 = DM(L_PAR);                     // a frame with the parameters; then pick the
        DM(REP + 2) = R0;                   // sound up again where the receiver is now
        R0 = DM(L_PAR + 1);
        DM(REP + 3) = R0;
        R0 = DM(L_PAR + 2);
        R1 = DM(L_PAR + 3);                 // or, asked with an address, the word there
        R1 = PASS R1;
        IF EQ JUMP shown;
        I4 = R1;
        L4 = 0;
        M0 = 0;
        R0 = DM(I4, M0);
shown:  DM(REP + 4) = R0;
        R0 = DM(L_BACK);
        R0 = LSHIFT R0 BY 16;
        R1 = DM(L_SKEW);
        R0 = R0 OR R1;
        DM(REP + 1) = R0;
        CALL frame;
        R0 = 0;
        DM(L_BACK) = R0;
        R1 = DM(IISP0A);
        R2 = L_RX;
        R1 = R1 - R2;
        R2 = LN - 2;
        R1 = R1 AND R2;
        DM(L_RP) = R1;
        RTS;

// One frame: the six input words at L_RP in, one output sample out.
live_tick:
        BIT SET FLAGS LINE;                 // high while a sample is worked on: see the top
        L0 = 0;
        M1 = 1;
        M2 = LN - 1;
        R0 = DM(L_RP);
        R1 = L_RX;
        R0 = R0 + R1;
        I0 = R0;
        R1 = -31;
        R8 = 0;
        R0 = DM(I0, M1);
        F0 = FLOAT R0 BY R1;
        F8 = F8 + F0;
        R0 = DM(I0, M2);
        F0 = FLOAT R0 BY R1;
        F8 = F8 + F0;
        R0 = DM(I0, M1);
        F0 = FLOAT R0 BY R1;
        F8 = F8 + F0;
        R0 = DM(I0, M2);
        F0 = FLOAT R0 BY R1;
        F8 = F8 + F0;
        R0 = DM(I0, M1);
        F0 = FLOAT R0 BY R1;
        F8 = F8 + F0;
        R0 = DM(I0, M1);
        F0 = FLOAT R0 BY R1;
        F8 = F8 + F0;
        CALL fx_gate;
        F0 = DM(L_PAR);
        F8 = F8 * F0;
        DM(L_IN) = F8;
        R0 = DM(L_PAR + 1);
        R0 = PASS R0;
        IF EQ JUMP dry;
        CALL nam_process;
        F8 = DM(L_OUT);
dry:    CALL fx_post;
        F0 = DM(L_PAR + 2);
        F8 = F8 * F0;
        F0 = 0.999;
        F8 = MIN(F8, F0);
        F0 = -0.999;
        F8 = MAX(F8, F0);
        R1 = 31;
        R0 = FIX F8 BY R1;
        R2 = DM(L_RP);
        R3 = DM(L_SKEW);
        R3 = R2 + R3;
        R1 = LN - 2;
        R3 = R3 AND R1;
        R1 = L_TX;
        R3 = R3 + R1;
        I0 = R3;                            // nam_process leaves L0 zero
        M1 = 1;
        M2 = LN - 1;
        DM(I0, M1) = R0;
        DM(I0, M2) = R0;
        DM(I0, M1) = R0;
        DM(I0, M2) = R0;
        DM(I0, M1) = R0;
        DM(I0, M1) = R0;
        R2 = R2 + 1;
        R2 = R2 + 1;
        R1 = LN - 2;
        R2 = R2 AND R1;
        DM(L_RP) = R2;
        BIT CLR FLAGS LINE;
        RTS;

// The effects around the model, one sample in F8 each. They use R0-R9, I0 and
// M0 and leave L0 zero.
.section pm 0x8c400
fx_reset:
        L0 = 0;
        M1 = 1;
        I0 = D_BUF;
        R0 = 0;
        LCNTR = D_MAX, DO fxclr UNTIL LCE;
fxclr:    DM(I0, M1) = R0;
        RTS;

// The gate. The envelope is the largest recent input, falling by L_PAR + 7 a
// sample; the gain goes towards 1 while it is above the threshold and towards
// 0 below.
fx_gate:
        F0 = ABS F8;
        F1 = DM(G_ENV);
        F2 = DM(L_PAR + 7);
        F1 = F1 * F2;
        F1 = MAX(F1, F0);
        DM(G_ENV) = F1;
        F2 = DM(L_PAR + 4);
        F3 = 1.0;
        R4 = 0;
        F5 = DM(L_PAR + 5);
        F6 = DM(L_PAR + 6);
        COMP(F1, F2);
        IF LT R3 = PASS R4;
        COMP(F1, F2);
        IF LT R5 = PASS R6;
        F7 = DM(G_GAIN);
        F3 = F3 - F7;
        F3 = F3 * F5;
        F7 = F7 + F3;
        DM(G_GAIN) = F7;
        F8 = F8 * F7;
        RTS;

// Three equaliser bands, then the delay: one tap, D_WP going round L_PAR + 23
// words of D_BUF, so what it reads is that many samples old.
fx_post:
        F0 = DM(EQ_C + 0);               // band 1
        F0 = F0 * F8;
        F1 = DM(EQ_S + 0);
        F9 = F0 + F1;                       // y
        F0 = DM(EQ_C + 0 + 1);
        F0 = F0 * F8;
        F1 = DM(EQ_C + 0 + 3);
        F1 = F1 * F9;
        F0 = F0 + F1;
        F1 = DM(EQ_S + 0 + 1);
        F0 = F0 + F1;
        DM(EQ_S + 0) = F0;
        F0 = DM(EQ_C + 0 + 2);
        F0 = F0 * F8;
        F1 = DM(EQ_C + 0 + 4);
        F1 = F1 * F9;
        F0 = F0 + F1;
        DM(EQ_S + 0 + 1) = F0;
        F8 = PASS F9;
        F0 = DM(EQ_C + 5);               // band 2
        F0 = F0 * F8;
        F1 = DM(EQ_S + 2);
        F9 = F0 + F1;                       // y
        F0 = DM(EQ_C + 5 + 1);
        F0 = F0 * F8;
        F1 = DM(EQ_C + 5 + 3);
        F1 = F1 * F9;
        F0 = F0 + F1;
        F1 = DM(EQ_S + 2 + 1);
        F0 = F0 + F1;
        DM(EQ_S + 2) = F0;
        F0 = DM(EQ_C + 5 + 2);
        F0 = F0 * F8;
        F1 = DM(EQ_C + 5 + 4);
        F1 = F1 * F9;
        F0 = F0 + F1;
        DM(EQ_S + 2 + 1) = F0;
        F8 = PASS F9;
        F0 = DM(EQ_C + 10);               // band 3
        F0 = F0 * F8;
        F1 = DM(EQ_S + 4);
        F9 = F0 + F1;                       // y
        F0 = DM(EQ_C + 10 + 1);
        F0 = F0 * F8;
        F1 = DM(EQ_C + 10 + 3);
        F1 = F1 * F9;
        F0 = F0 + F1;
        F1 = DM(EQ_S + 4 + 1);
        F0 = F0 + F1;
        DM(EQ_S + 4) = F0;
        F0 = DM(EQ_C + 10 + 2);
        F0 = F0 * F8;
        F1 = DM(EQ_C + 10 + 4);
        F1 = F1 * F9;
        F0 = F0 + F1;
        DM(EQ_S + 4 + 1) = F0;
        F8 = PASS F9;
        L0 = 0;
        M0 = 0;
        R0 = DM(D_WP);
        R2 = D_BUF;
        R2 = R2 + R0;
        I0 = R2;
        F1 = DM(I0, M0);                    // the delayed sample
        F2 = DM(L_PAR + 24);
        F2 = F2 * F1;
        F2 = F2 + F8;
        DM(I0, M0) = F2;
        F2 = DM(L_PAR + 25);
        F2 = F2 * F1;
        F8 = F8 + F2;
        R0 = R0 + 1;
        R1 = DM(L_PAR + 23);
        COMP(R0, R1);
        IF GE R0 = R0 - R0;
        DM(D_WP) = R0;
        RTS;
