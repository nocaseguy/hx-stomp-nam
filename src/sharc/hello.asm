// hello.asm - first program of our own on the SHARC: proves the boot path.
//
// Drives FLAG2 and FLAG3 as outputs and toggles them forever, FLAG3 at half
// the rate of FLAG2. One of them is the line the LPC4350 reads on GPIO7[0]
// (low = "DSP ready" in the stock firmware), so the host sees a square wave.
// No PLL, no external memory, no interrupts.

.equ FLG2,  0x10
.equ FLG3,  0x40
.equ OUTS,  0xa0                // FLAG2 and FLAG3 output enables

.section pm 0x8c100
start:  BIT SET FLAGS OUTS;
        BIT CLR FLAGS FLG2 | FLG3;
loop:   CALL wait;
        BIT TGL FLAGS FLG2;
        CALL wait;
        BIT TGL FLAGS FLG2 | FLG3;
        JUMP loop;

wait:   R0 = 0x400;             // 0x400 * 0x10000 loop passes
outer:  LCNTR = 0xffff, DO inner UNTIL LCE;
inner:    NOP;
        R0 = R0 - 1;
        IF NE JUMP outer;
        RTS;
