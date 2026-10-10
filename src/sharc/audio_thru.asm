// audio_thru.asm - the first sound through our own code: inputs to outputs.
//
// Assembled with report.asm and audio.asm. After the set-up it sends three
// frames of one word, "THRU", so that the LPC4350 can tell it got this far,
// and then only moves samples: every 48 kHz frame the three 48 kHz inputs are
// added together (saturating) and sent to all three 48 kHz outputs, left with
// left and right with right, and the 192 kHz input goes to the 192 kHz output.
// Which jack is on which port does not have to be known for that.
//
// Each word is sent one word late. A received word is complete half a bit
// clock before the transmitter takes its next one, and this loop is quick
// enough to get in there: sent at once, a left word went out in the right
// slot (heard on the unit: everything in the right channel). Held back one
// word, it is written as the next right word arrives and goes out left.
// That is good enough for listening; anything that takes longer than a
// fraction of a microsecond a sample needs DMA.

.equ REP_WORDS, 1
.equ ALUSAT,  0x2000            // MODE1: fixed-point adds saturate

.section dm 0xc0400
REP_UNIT: .word 0x40000
REP:    .word 0x54485255

.section pm 0x8c100
start:  BIT SET FLAGS OUTS;
        BIT CLR FLAGS LINE;
        R7 = 0x40000;
        R4 = 16;
        CALL wait;
        CALL mark;
        CALL pll;
        CALL audio_init;
        CALL audio_ports;
        CALL frame;
        CALL frame;
        CALL frame;
        BIT SET MODE1 ALUSAT;
        R3 = 0xc0000000;
        R4 = 0x18000000;
        R6 = 0;
        R7 = 0;
thru:   R0 = DM(SPCTL0);
        R1 = R0 AND R3;
        IF EQ JUMP fast;
        R2 = DM(RXSP0A);                    // the three ports run off one clock: their
        R1 = R0 AND R4;                     // words arrive together
        IF EQ JUMP no0b;
        R5 = DM(RXSP0B);
        R2 = R2 + R5;
no0b:   R0 = DM(SPCTL1);
        R1 = R0 AND R3;
        IF EQ JUMP no1a;
        R5 = DM(RXSP1A);
        R2 = R2 + R5;
no1a:   DM(TXSP4A) = R6;
        DM(TXSP4B) = R6;
        DM(TXSP6A) = R6;
        R6 = PASS R2;
fast:   R0 = DM(SPCTL2);
        R1 = R0 AND R3;
        IF EQ JUMP thru;
        R2 = DM(RXSP2A);
        DM(TXSP5A) = R7;
        R7 = PASS R2;
        JUMP thru;
