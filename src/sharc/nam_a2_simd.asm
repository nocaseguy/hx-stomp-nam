// nam_a2_simd.asm - NAM A2-Full (8 channels) using both processing elements.
//
// Same arithmetic as nam_a2.asm, two output channels at a time: PEx computes
// channel 2p while PEy computes channel 2p+1. Inputs are read through I1 with
// broadcast loads so both elements see the same value; weights are stored
// interleaved (channel 2p, channel 2p+1) and read in pairs through I8.
//
// What keeps the instruction count near the multiply count:
//   - a layer's taps are next to each other in its ring, oldest first, so the
//     dot product reads them where they are. A convolution with dilation d
//     wants the samples from d, 2d, .. (K-1)d ago. The ring has (K-1)d + 1
//     places of C words and each sample is written (K-1)(d-1) + 1 places on
//     from the one before, going round; d such steps come to one place more
//     than a whole number of turns, so the sample from d ago is always in the
//     place just before. After a write the pointer is moved on to where the
//     next sample will go, and that is also where this sample's oldest tap
//     starts: K places back, going round.
//   - the dot loops read the ring through a circular I1 with broadcast loads
//     and step back to the oldest tap for the next pair of channels in the
//     instruction that adds the last product. The sum starts from the bias
//     plus the mix-in weight times the input sample, both read from the layer
//     table. Each row of convolution weights ends with a spare pair because
//     the loop fetches one pair ahead.
//   - the residual 1x1 starts its sum from x, and each result is written,
//     during the next pair's last multiply, both to x and into the next
//     layer's ring, so no layer copies x into its ring. Its multiplies are a
//     loop and not straight-line code, although that is four instructions a
//     layer more: every instruction that reads data through PM needs a place
//     in the 32-entry instruction cache, and with more such instructions than
//     places each one costs a cycle each time (measured on the unit).
//   - activations are kept per layer and summed for the head once per sample
// The pairing of the convolution and of the 1x1 is written out for 8 channels.
//
// Model block at MODEL, from place() in tools/sharc/nam_pack.py. The header,
// the layer table and the head ring are there, next to the scratch area. The
// dot loop reads a layer's ring through DM and its weights through PM in one
// instruction, so place() puts each layer's ring and weights in different
// memory blocks, and neither in the block of the scratch area.
//
//   MODEL+0 C          +1 layers        +2 -> layer table   +3 -> rechannel[C]
//        +4 -> head weights w[tap][ch]  +5 head scale       +6 head ring base
//        +7 head ring length (16*C)     +8 head ring write pointer, a spare word
//        +10 head bias
//   layer table, in the order it is read:
//        a ring: base, length, 2 + (K-1)(d-1)*C, write pointer, a spare word
//        then for every layer: K*C, -(K*C+1), -> weights, for each pair of
//        channels {mixin, mixin, bias, bias}, and the next ring
//        (the last layer's 1x1 output goes to a ring of C words nothing reads)
//   The spare words are there because the pointers are stored while SIMD is
//   on, and a store of a register that has no partner in the other processing
//   element then overwrites the following word too.
//   layer weights: C/2 rows of pairs {w[tap][ch].., a spare pair},
//                  then C/2 rows of C+1 pairs {w[ch].., bias}
//
// nam_reset: clears every ring and rewinds its pointer; call it once after
// the model is loaded (the rings are not part of what is loaded).
// nam_process: NAM_IN -> input floats, NAM_OUT -> output floats, NAM_N samples.
// Needs MODE1 with RND32 and circular buffering on; returns with SIMD and
// broadcast off and L0, L1, L13 zero. In the SIMD part data registers are
// moved only through index registers, where the pairing rule is simple.
// Neither routine uses stack memory. An I, M, L or B register of DAG1 is never
// loaded through DAG1 with post-modify or stored through it: that does not
// work on the processor (sasm refuses it).

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
.equ H_HBIAS,   10
.equ MAXCH,     8
.equ MAXTAPS,   15
.equ MAXLAYERS, 23
.equ PEYEN,     0x200000         // MODE1: second processing element on
.equ BDCST1,    0x800000         // MODE1: loads through I1 go to both elements

.section dm 0xc0000
NAM_IN:   .space 1
NAM_OUT:  .space 1
NAM_N:    .space 1
          .space 1                      // keeps X on an even address
X:        .space MAXCH                  // the signal between layers
AL:       .space MAXLAYERS*MAXCH + 2    // every layer's activations (then 1.0 after the current one)

.section pm 0x8c200
nam_process:
        L0 = 0; L1 = 0; L2 = 0; L3 = 0; L4 = 0; L5 = 0; L6 = 0; L7 = 0;
        L8 = 0; L13 = 0;
        M0 = 0; M1 = 1; M3 = 2; M9 = 1; M10 = -2; M11 = 2;
        F5 = 0.01; S5 = 0.01;               // leaky ReLU slope, both elements
        R3 = 0; S3 = 0;                     // zero, both elements
        F7 = 1.0;
        R0 = DM(MODEL + H_C);
        M5 = R0;                            // C
        R11 = LSHIFT R0 BY -1;              // C/2
        R0 = R0 + 1;
        M6 = R0;                            // C+1
        R10 = DM(MODEL + H_HLEN);
        R10 = LSHIFT R10 BY -1;             // 16*C/2
        R1 = DM(MODEL + H_LAYERS);
        I6 = DM(NAM_IN);
        I7 = DM(NAM_OUT);
        R0 = DM(NAM_N);
        LCNTR = R0, DO sample UNTIL LCE;
          F15 = DM(I6, M1);
          S15 = R15;                        // the input sample, in both elements
          I8 = DM(MODEL + H_RECH);
          I5 = X;
          LCNTR = M5, DO rech UNTIL LCE;    // x = rechannel * in
            F4 = PM(I8, M9);
            F8 = F4 * F15;
rech:       DM(I5, M1) = F8;
          I4 = DM(MODEL + H_TABLE);
          I2 = AL;
          BIT SET MODE1 PEYEN | BDCST1;
          NOP;                              // the mode change takes a cycle
          B13 = DM(I4, M1);                 // first layer: x into its ring
          L13 = DM(I4, M1);
          M12 = DM(I4, M1);
          I13 = DM(I4, M0);
          I5 = X;
          F0 = DM(I5, M3);
          LCNTR = R11, DO push UNTIL LCE;
push:       PM(I13, M11) = F0, F0 = DM(I5, M3);
          MODIFY(I13, M10);                 // back over the last pair, then on to where
          MODIFY(I13, M12);                 // the next sample goes = this one's oldest tap
          DM(I4, M3) = I13;                 // and over the spare word
          LCNTR = R1, DO layer UNTIL LCE;
            R13 = DM(I4, M1);               // K*C
            M2 = DM(M0, I4);                // -(K*C+1): a DAG1 register, loaded without post-modify
            MODIFY(I4, M1);
            I8 = DM(I4, M1);
            B1 = B13;                       // this layer's ring, as its writer left it
            L1 = L13;
            I1 = I13;
            I0 = I2;                        // where this layer's activations go

            // a = lrelu(bias + mixin * in + w . taps), channels 0,1. The mix-in
            // weights and biases come from the table, a pair of each for a pair
            // of channels, so that only two instructions a pair use PM.
            F4 = DM(I4, M3);
            F12 = F15 * F4, F8 = DM(I4, M3), F4 = PM(I8, M11);  // mixin * in; bias; first weights
            F0 = DM(I1, M1);
            LCNTR = R13, DO dot0 UNTIL LCE;
dot0:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F8 = F8 + F12, F0 = DM(I1, M2);     // and back to the oldest tap
            F12 = F8 * F5;
            F9 = MAX(F8, F12), F4 = DM(I4, M3);
            // channels 2,3; the previous pair is stored on the way
            F12 = F15 * F4, F8 = DM(I4, M3), F4 = PM(I8, M11);  // mixin * in; bias; first weights
            F0 = DM(I1, M1);
            LCNTR = R13, DO dot1 UNTIL LCE;
dot1:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F8 = F8 + F12, F0 = DM(I1, M2);     // and back to the oldest tap
            F12 = F8 * F5, DM(I2, M3) = F9;
            F9 = MAX(F8, F12), F4 = DM(I4, M3);
            // channels 4,5
            F12 = F15 * F4, F8 = DM(I4, M3), F4 = PM(I8, M11);  // mixin * in; bias; first weights
            F0 = DM(I1, M1);
            LCNTR = R13, DO dot2 UNTIL LCE;
dot2:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F8 = F8 + F12, F0 = DM(I1, M2);     // and back to the oldest tap
            F12 = F8 * F5, DM(I2, M3) = F9;
            F9 = MAX(F8, F12), F4 = DM(I4, M3);
            // channels 6,7
            F12 = F15 * F4, F8 = DM(I4, M3), F4 = PM(I8, M11);  // mixin * in; bias; first weights
            F0 = DM(I1, M1);
            LCNTR = R13, DO dot3 UNTIL LCE;
dot3:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F8 = F8 + F12, F0 = DM(I1, M2);     // and back to the oldest tap
            F12 = F8 * F5, DM(I2, M3) = F9;
            F9 = MAX(F8, F12);
            DM(I2, M3) = F9;
            DM(I2, M0) = F7;                // 1.0 after the activations: the 1x1 bias term

            // x += w . {a, 1}; results go to x and into the next layer's ring
            B1 = I0;
            L1 = M6;
            I0 = X;
            I5 = X;
            B13 = DM(I4, M1);
            L13 = DM(I4, M1);
            M12 = DM(I4, M1);
            I13 = DM(I4, M0);
            // channels 0,1 in F8
            F8 = DM(I5, M3);
            R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
            LCNTR = M5, DO res0 UNTIL LCE;
res0:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F12 = F0 * F4, F8 = F8 + F12;
            // channels 2,3 in F9
            F8 = F8 + F12, F9 = DM(I5, M3);
            R14 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
            LCNTR = M5, DO res2 UNTIL LCE;
res2:         F14 = F0 * F4, F9 = F9 + F14, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F14 = F0 * F4, F9 = F9 + F14, DM(I0, M3) = F8, PM(I13, M11) = F8;
            // channels 4,5 in F8
            F9 = F9 + F14, F8 = DM(I5, M3);
            R12 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
            LCNTR = M5, DO res1 UNTIL LCE;
res1:         F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F12 = F0 * F4, F8 = F8 + F12, DM(I0, M3) = F9, PM(I13, M11) = F9;
            // channels 6,7 in F9
            F8 = F8 + F12, F9 = DM(I5, M3);
            R14 = R3 - R3, F0 = DM(I1, M1), F4 = PM(I8, M11);
            LCNTR = M5, DO res3 UNTIL LCE;
res3:         F14 = F0 * F4, F9 = F9 + F14, F0 = DM(I1, M1), F4 = PM(I8, M11);
            F14 = F0 * F4, F9 = F9 + F14, DM(I0, M3) = F8, PM(I13, M11) = F8;
            F9 = F9 + F14;
            DM(I0, M3) = F9, PM(I13, M12) = F9; // and on to where the next sample goes
layer:      DM(I4, M3) = I13;               // the next ring's pointer, now at its oldest tap

          BIT CLR MODE1 BDCST1;
          NOP;
          B0 = DM(MODEL + H_HBASE);
          L0 = DM(MODEL + H_HLEN);
          I0 = DM(MODEL + H_HWP);
          I4 = AL;
          LCNTR = R11, DO hsum UNTIL LCE;   // head input: activations summed over the layers
            I2 = I4;
            MODIFY(I4, M3);
            R8 = R3 + R3, F0 = DM(I2, M5);
            LCNTR = R1, DO hacc UNTIL LCE;
hacc:         F8 = F8 + F0, F0 = DM(I2, M5);
hsum:       DM(I0, M3) = F8;
          DM(MODEL + H_HWP) = I0;
          I8 = DM(MODEL + H_HEAD);          // head: PEx takes even terms, PEy odd
          NOP;                              // keeps the next two out of a full set of the
                                            // instruction cache (tools/sharc/pmcache.py)
          R8 = R3 + R3, R12 = R3 - R3, F0 = DM(I0, M3), F4 = PM(I8, M11);
          LCNTR = R10, DO hdot UNTIL LCE;
hdot:       F12 = F0 * F4, F8 = F8 + F12, F0 = DM(I0, M3), F4 = PM(I8, M11);
          F8 = F8 + F12;
          L0 = 0;
          L1 = 0;
          L13 = 0;
          BIT CLR MODE1 PEYEN;
          NOP;
          R0 = S8;                          // add the two elements' halves
          F8 = F8 + F0;
          F0 = DM(MODEL + H_HBIAS);
          F8 = F8 + F0;
          F4 = DM(MODEL + H_SCALE);
          F8 = F8 * F4;
sample:   DM(I7, M1) = F8;
        RTS;

nam_reset:
        L0 = 0; L4 = 0; L9 = 0;
        M1 = 1; M2 = 5; M3 = 16; M9 = 1;
        R0 = 0;
        I0 = DM(MODEL + H_HBASE);
        DM(MODEL + H_HWP) = I0;
        R1 = DM(MODEL + H_HLEN);
        LCNTR = R1, DO zhead UNTIL LCE;
zhead:    DM(I0, M1) = R0;
        I4 = DM(MODEL + H_TABLE);
        R2 = DM(MODEL + H_LAYERS);
        R2 = R2 + 1;                        // and the ring after the last layer
        LCNTR = R2, DO zrings UNTIL LCE;
          I9 = DM(I4, M1);                  // through DAG2: I0 could not be loaded this way
          R1 = DM(I4, M1);
          MODIFY(I4, M1);
          DM(I4, M2) = I9;                  // pointer = base; on to the next ring's entry
          MODIFY(I4, M3);
          LCNTR = R1, DO zring UNTIL LCE;
zring:      PM(I9, M9) = R0;
zrings:   NOP;
        RTS;
