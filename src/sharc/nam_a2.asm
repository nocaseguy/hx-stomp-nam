// nam_a2.asm - Neural Amp Modeler A2 block for the SHARC, any channel count.
//
// Same arithmetic as src/dsp/nam_a2.c, one sample at a time; the layout of the
// model block at MODEL is produced by tools/sharc/nam_pack.py:
//
//   MODEL+0 C          +1 layers        +2 -> layer table   +3 -> rechannel[C]
//        +4 -> head: bias, then w[tap][ch]                  +5 head scale
//        +6 head ring base   +7 head ring length (16*C)     +8 head ring write pointer
//   layer table, 7 words a layer:
//        ring base, ring length, write pointer, (dilation-1)*C, K, K*C, -> weights
//   layer weights: C x {bias, mixin, w[tap][ch] (K*C)}, then C x {bias, w[ch] (C)}
//
// A layer's ring holds exactly (K-1)*dilation+1 columns, so after the newest
// column is written the pointer is already at the oldest tap.
//
// nam_process: NAM_IN -> input floats, NAM_OUT -> output floats, NAM_N samples.
// Needs MODE1 with RND32 and circular buffering on; uses no stack memory and
// leaves L0 and L1 zero.

.equ MODEL,     0xc0100          // the model block follows the scratch area
.equ H_C,       0
.equ H_LAYERS,  1
.equ H_TABLE,   2
.equ H_RECH,    3
.equ H_HEAD,    4
.equ H_SCALE,   5
.equ H_HBASE,   6
.equ H_HLEN,    7
.equ H_HWP,     8
.equ MAXCH,     8
.equ MAXTAPS,   15

.section dm 0xc0000
NAM_IN:   .space 1
NAM_OUT:  .space 1
NAM_N:    .space 1
X:        .space MAXCH              // the signal between layers
A:        .space MAXCH              // a layer's activations
HS:       .space MAXCH              // sum of activations: the head's input
G:        .space MAXTAPS*MAXCH      // a layer's taps, oldest first

.section pm 0x8c200
nam_process:
        L0 = 0; L1 = 0; L2 = 0; L3 = 0; L4 = 0; L5 = 0; L6 = 0; L7 = 0;
        L8 = 0; L9 = 0;
        M0 = 0; M1 = 1; M3 = 2; M9 = 1; M10 = -1;
        F5 = 0.01;                          // leaky ReLU slope
        R14 = DM(MODEL + H_C);
        I6 = DM(NAM_IN);
        I7 = DM(NAM_OUT);
        R0 = DM(NAM_N);
        LCNTR = R0, DO sample UNTIL LCE;
          F15 = DM(I6, M1);
          I8 = DM(MODEL + H_RECH);
          I5 = X;
          I3 = HS;
          R12 = R12 - R12;
          LCNTR = R14, DO rech UNTIL LCE;   // x = rechannel * in, hs = 0
            F4 = PM(I8, M9);
            F8 = F4 * F15;
            DM(I5, M1) = F8;
rech:       DM(I3, M1) = R12;
          I4 = DM(MODEL + H_TABLE);
          R0 = DM(MODEL + H_LAYERS);
          LCNTR = R0, DO layer UNTIL LCE;
            B0 = DM(M0, I4);                // DAG1 registers: loaded without post-modify
            L0 = DM(M1, I4);
            MODIFY(I4, M3);
            I0 = DM(M0, I4);
            I5 = X;
            LCNTR = R14, DO push UNTIL LCE; // newest column into the ring
              F0 = DM(I5, M1);
push:         DM(I0, M1) = F0;
            R0 = I0;
            DM(I4, M1) = R0;
            M2 = DM(M0, I4);
            MODIFY(I4, M1);
            R6 = DM(I4, M1);
            R13 = DM(I4, M1);
            I8 = DM(I4, M1);
            I9 = G;
            LCNTR = R6, DO taps UNTIL LCE;  // gather the K taps
              LCNTR = R14, DO tap UNTIL LCE;
                F0 = DM(I0, M1);
tap:            PM(I9, M9) = F0;
taps:         MODIFY(I0, M2);
            L0 = 0;
            I2 = A;
            I3 = HS;
            LCNTR = R14, DO conv UNTIL LCE; // a = lrelu(bias + mixin*in + w . taps)
              F8 = PM(I8, M9);
              F4 = PM(I8, M9);
              F12 = F4 * F15;
              F8 = F8 + F12;
              I1 = G;
              R12 = R12 - R12, F0 = DM(I1, M1), F4 = PM(I8, M9);
              LCNTR = R13, DO dot UNTIL LCE;
dot:            F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M9);
              F8 = F8 + F12;
              MODIFY(I8, M10);              // the loop fetched one weight too many
              F12 = F8 * F5;
              F8 = MAX(F8, F12);
              DM(I2, M1) = F8;
              F12 = DM(I3, M0);
              F12 = F12 + F8;
conv:         DM(I3, M1) = F12;
            I5 = X;
            LCNTR = R14, DO res UNTIL LCE;  // x += bias + w . a
              F8 = PM(I8, M9);
              I1 = A;
              R12 = R12 - R12, F0 = DM(I1, M1), F4 = PM(I8, M9);
              LCNTR = R14, DO dot2 UNTIL LCE;
dot2:           F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M9);
              F8 = F8 + F12;
              MODIFY(I8, M10);
              F12 = DM(I5, M0);
              F12 = F12 + F8;
res:          DM(I5, M1) = F12;
layer:      NOP;
          B1 = DM(MODEL + H_HBASE);         // head: 16 taps over the activation sums
          L1 = DM(MODEL + H_HLEN);
          I1 = DM(MODEL + H_HWP);
          I3 = HS;
          LCNTR = R14, DO hpush UNTIL LCE;
            F0 = DM(I3, M1);
hpush:      DM(I1, M1) = F0;
          R0 = I1;
          DM(MODEL + H_HWP) = R0;
          I8 = DM(MODEL + H_HEAD);
          R13 = DM(MODEL + H_HLEN);
          F8 = PM(I8, M9);
          R12 = R12 - R12, F0 = DM(I1, M1), F4 = PM(I8, M9);
          LCNTR = R13, DO hdot UNTIL LCE;
hdot:       F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M9);
          F8 = F8 + F12;
          F4 = DM(MODEL + H_SCALE);
          F8 = F8 * F4;
          L1 = 0;
sample:   DM(I7, M1) = F8;
        RTS;
