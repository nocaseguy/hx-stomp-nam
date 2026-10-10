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
// is reading, at the output level and clamped just short of full scale. The 192 kHz ports are left off.
//
// Before it starts it sends three frames on the report line: "LIVE", the
// distance between the transmit and receive rings it measured, and the
// parameters.
//
// Parameters come from the LPC4350 over the SPI port the program was booted
// through, two words a message, received by DMA the way the boot kernel
// receives (src/smoke_sharc.c sends them): a number 0..3 and a value.
//     0  input gain (float)            1  model: 0 = off, the input goes straight on
//     2  output level (float)          3  send a frame with the parameters now
//                                         (the sound stops for a second)

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

.section dm 0xc0400
REP_UNIT: .word 0x40000
REP:    .word 0x4c495645
        .space 4
L_PAR:  .float 1.0              // 0: input gain
        .word 1                 // 1: model on
        .float 0.25             // 2: output level: 12 dB down
        .word 0                 // 3: (a request, not a value)
L_MSG:  .space 2                // a message: parameter number, value
L_RP:   .word 0                 // next frame to process: an even offset into the receive rings
L_SKEW: .word 0                 // offset of the place to write from the place read
L_IN:   .space 1
L_OUT:  .space 1

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
        R1 = 3;
        R0 = R0 AND R1;
        R2 = L_PAR;
        R2 = R2 + R0;
        I4 = R2;
        L4 = 0;
        M0 = 0;
        R2 = DM(L_MSG + 1);
        DM(I4, M0) = R2;
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
        CALL live_tick;
        JUMP live;

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
        DM(REP + 4) = R0;
        CALL frame;
        R1 = DM(IISP0A);
        R2 = L_RX;
        R1 = R1 - R2;
        R2 = LN - 2;
        R1 = R1 AND R2;
        DM(L_RP) = R1;
        RTS;

// One frame: the six input words at L_RP in, one output sample out.
live_tick:
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
        F0 = DM(L_PAR);
        F8 = F8 * F0;
        DM(L_IN) = F8;
        R0 = DM(L_PAR + 1);
        R0 = PASS R0;
        IF EQ JUMP dry;
        CALL nam_process;
        F8 = DM(L_OUT);
dry:    F0 = DM(L_PAR + 2);
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
        RTS;
