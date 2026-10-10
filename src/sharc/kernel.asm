// kernel.asm - our boot kernel for the ADSP-21469 in SPI slave boot mode.
//
// After reset the processor itself receives 384 32-bit words over SPI into
// the first 256 instruction slots (0x8c000..0x8c0ff), takes the SPI interrupt
// at 0x8c030 and resumes at 0x8c005. This kernel then keeps receiving records
// through the same SPI DMA channel:
//
//     address, count, <count words>      load `count` 32-bit words at `address`
//     entry,   0                         jump to the 48-bit address `entry`
//
// Addresses are 32-bit normal-word addresses, so code is loaded through the
// 32-bit view of its memory (tools/sharc/mkboot.py does the conversion).
// It sits at the end of the vector area so a program can load its own vector
// table over 0x8c000..0x8c0af; it does not touch the PLL or external memory.

.equ IISPI,   0x1080            // SPI DMA address
.equ IMSPI,   0x1081            // SPI DMA address step
.equ CSPI,    0x1082            // SPI DMA word count; writing it starts a transfer
.equ SPIDMAC, 0x1084            // bit 15: DMA transfer in progress
.equ HDR,     0x9217e           // record header: the 32-bit words behind 0x8c0fe/0x8c0ff

.section pm 0x8c004
        NOP;
        JUMP kstart;

.section pm 0x8c030             // SPI interrupt: boot DMA done
        RTI;

.section pm 0x8c0b0
kstart: IMASK = 0;
        IRPTL = 0;
        M13 = 0;
        R4 = 1;
        DM(IMSPI) = R4;
next:   R0 = HDR;
        R1 = 2;
        CALL recv;
        R0 = DM(HDR);
        R1 = DM(HDR + 1);
        R1 = PASS R1;
        IF EQ JUMP run;
        CALL recv;
        JUMP next;
run:    I12 = R0;
        JUMP (M13, I12);

recv:   DM(IISPI) = R0;         // receive R1 words at R0
        DM(CSPI) = R1;
        NOP;
        NOP;
busy:   USTAT1 = DM(SPIDMAC);
        BIT TST USTAT1 0x8000;
        IF TF JUMP busy;
        RTS;
