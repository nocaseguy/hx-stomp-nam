# HX Stomp: SHARC-side hardware facts

> **Which unit.** Everything marked as found or working "on the unit" was done
> on an **HX Stomp** (the three-footswitch model, boards marked P33). The
> register values and pin numbers were first read from the **HX Stomp XL**
> firmware (v3.80.0, P36) and then checked against the HX Stomp's own
> (v3.80.0, P33): the SHARC program and the audio routing data are the same
> file byte for byte, the bootloader differs only in its splash picture and
> model name, and in the main application every routine these notes rest on
> (board and audio pin set-up, SHARC boot, the two I2C devices, the directly
> read inputs, return to DFU) is identical instruction for instruction. The
> one difference found: the footswitch-light message takes switch numbers
> 0-3 on the HX Stomp and 0-8 on the XL. Nothing here has been run on an XL.


What our own SHARC program has to set up, read from the register values the
stock program (v3.80.0) writes. Only values are recorded here; the code that
uses them is ours (`src/sharc/`). Register names are from memory of the
ADSP-2146x manuals and are marked `?` where unsure; the addresses and values
are exact.

## Found by running our own code on the unit

- The boot path works as described under "Boot" below: our kernel, records
  over SSP0 at 2 MHz, records of 12,000 words included.
- With no PLL set-up the core runs at 400 MHz (CLKIN x 16). The stock PLL
  sequence below, run by `nam_bench.asm`, shortens a timed loop by exactly
  16/18 (45,752 polls to 40,669), so it takes the core to 450 MHz and CLKIN is
  25 MHz.
- GPIO7[0] on the LPC4350 follows FLAG2/FLAG3 (both are driven together).
- **An I, M, L or B register cannot be loaded from memory through an index
  register of its own address generator with post-modify, nor stored through
  it.** `I0 = DM(I4, M1)` left I0 unchanged; the stock program has 3,800 loads
  of DAG1 registers through DAG1, every one of them pre-modify without update
  (`I3 = DM(M6, I6)`, `I2 = DM(2, I7)`), and no such store at all. Loading a
  DAG2 register through DAG1 with post-modify is fine. `sasm` refuses the bad
  forms and `ssim` stops on them.
- **In SIMD mode a store of a register with no partner in the other
  processing element (I, M, L, B) also overwrites the next memory word**:
  `DM(MODEL + 8) = I0` left a zero in MODEL + 9. Loads of such registers in
  SIMD mode are as expected. `ssim` writes junk to that word.
- A loop count loaded from memory by the instruction just before
  `LCNTR = Rn, DO ...` is used correctly.

- **The audio master clock is there without the LPC4350 doing anything**:
  with the routing and clock-generator values below, `audio_probe.asm` counts
  14,318 words in 150 ms on SPORT0 A, SPORT0 B and SPORT1 A (48.0 kHz stereo)
  and 57,267 on SPORT2 A (192 kHz). So the clock on DAI pin 7 is 49.152 MHz,
  domain B is 48 kHz and domain A 192 kHz.
- The serial-port registers `audio_probe.asm` uses from memory are right:
  receive data at control + 0x61 (A) and + 0x63 (B), data waiting in control
  bits 31:30 (A) and 28:27 (B), control 0x000029f1 for I2S receive without DMA.
- The converters are three Cirrus **CS4272** codecs in stand-alone mode
  (read off board photos). They must be held in reset until their clocks are
  stable, and **their reset is DPI pin 7 of the SHARC**: the stock routing
  drives it low, and the stock program sets its source high
  (`DM(0x1c11) = old & 0xfffff03f | 0x40`) right after starting the clock
  generators. Left low, or never driven, every input delivers zeros and the
  outputs are mute. With that done a test tone sent on the transmitting ports
  is heard at the main output (bypass relays switched, see `/HARDWARE.md`).
- **Serial port DMA with chaining works as the stock program uses it**: a
  transfer control block is four words {chain pointer, count, step, start},
  the chain pointer is the address of the next block's last word & 0x7ffff
  (| 0x80000 for an interrupt, not used by us), the port's CP register
  (control + 0x43 for A, + 0x47 for B) is loaded with that value and the
  control word has 0xc0000 (A) / 0x300000 (B) set. A block that points at
  itself is a ring; the current position reads back from control + 0x40 /
  + 0x44. `nam_live.asm` runs six ports that way.
- Internal loopback (SPORT0 A data in = SPORT4 A data out, source code 0x1c in
  0x2440) returns the transmitted words, so the transmit registers from
  memory are right too: control + 0x60 (A), + 0x62 (B), control 0x020029f1.
- GPIO7[0] of the LPC4350 follows FLAG3 alone; the stock program sets FLAG2
  high once and leaves it.

## Core clock (PMCTL, 0x2000)

Sequence, each step followed by a delay loop (16 or 4096 iterations):

1. clear 0xc0, set 0x240
2. clear 0x200, set 0x8100 (bypass on), wait 4096
3. clear 0x8000, wait 16
4. clear 0x13f, set 0x8012 (multiplier field = 18, bypass on), wait 4096
5. clear 0x8000, wait 16
6. clear 0x7c00c0, set 0x400200, wait 16

Final state: multiplier 18, input divider off, output divider code 0, bit 22
set in the 22:18 field (DDR2 clock ratio ?). The stock program loads the core
timer with 450,000 counts per millisecond, so it runs the core at 450 MHz and
CLKIN is 25 MHz (18 x 25).

Before the PLL: `DM(0x4c20) = 8`, `DM(0x4c00) = 0` (link ports off ?).

## DDR2 (external memory at 0x200000)

In order:

| address | value | note |
|---|---|---|
| 0x1851, 0x1856 | set bits 0xe00 | both DLLs; then wait 8 and 3000 loops |
| 0x1801 | 0x00000001 | bank 0 is DDR2 ? |
| 0x181d | 0x05a006ce | |
| 0x1813 | 0x18d0448a | timing |
| 0x1814 | 0x00000642 | mode register ? |
| 0x1815 | 0x00004000 | |
| 0x1816 | 0x00008000 | |
| 0x1817 | 0x0000c000 | |
| 0x1812 | 0x18018a48 | control; starts initialisation |
| 0x181e | wait for bit 0x100 | |

then one dummy read of 0x200000. Later in start-up: 0x2200 = 0x0a821c66,
0x2201 = 0x241d6570, 0x2202 = 0x000ddc13 (purpose unknown), and SYSCTL
(0x30024) = (old & 0xffff) & 0xfff0ffff.

## Audio clocks (PCG, 0x24c0..)

| address | value | |
|---|---|---|
| 0x24c1 | 0xc0200004 | clock A = external / 4 |
| 0x24c3 | 0xc0800010 | clock B = external / 16 |
| 0x24c4 | 0x02000080 | pulse widths |
| 0x24c0 | 0xc0000100 | frame sync A = external / 256, enable |
| 0x24c2 | 0xc0000400 | frame sync B = external / 1024, enable |
| 0x24c6..0x24c9 | 0 | |

The external clock for both is DAI pin 7 (0x2434 below). Both domains carry
64 bit clocks per frame (two 32-bit slots) and domain A runs at four times the
rate of domain B. The master clock frequency is not known from the firmware:
49.152 MHz would give 192 kHz / 48 kHz, 12.288 MHz 48 kHz / 12 kHz. Measure
DAI pin 7 or the frame syncs.

## Signal routing (SRU)

`address mask value`: write `(old & ~mask) | value`.

```
0x2430 0x3ff07fff 0x39d073bd   SPORT0,1,4 clock = PCG B; SPORT2,5 clock = PCG A
0x2450 0x3ff07fff 0x39d073bd   same for the frame syncs
0x2435 0x0000001f 0x0000001d   SPORT6 clock = PCG B
0x2454 0x0000001f 0x0000001d   SPORT6 frame sync = PCG B
0x2434 0x000003ff 0x000000c6   PCG A and B external clock = DAI pin 7
0x2440 0x3f03ffff 0x0e005491   SPORT0 A,B data in = DAI pins 18,19; SPORT1 A = pin 6; SPORT2 A = pin 15
0x2460 0x0fffffff 0x074b0e9c   DAI pin 1 = SPORT4 A out, 2 = SPORT4 B out, 3 = code 0x2c, 4 = PCG frame sync A
0x2461 0x0fffffff 0x0fdfbf7e   pins 5-8: not driven
0x2462 0x0fffffff 0x0fcedcb8   pin 9 = PCG clock A, 10 = PCG clock B, 11 = PCG frame sync B
0x2463 0x0fffffff 0x0fdfbf7e   pins 13-16: not driven
0x2464 0x0fffffff 0x03dfbf38   pin 17 = PCG clock A, 20 = SPORT5 A out
0x2478 0x3fffffff 0x00041041   output enable: pins 1,2,3,4
0x2479 0x3fffffff 0x01040000   output enable: pins 9,10
0x247a 0x3fffffff 0x00000001   output enable: pin 11
0x247b 0x3fffffff 0x01000040   output enable: pins 17,20
0x1c00 0x01fffc1f 0x00221402   DPI routing (SPI, SPI B, ... to the LPC4350)
0x1c01 0x000003ff 0x00000085
0x1c10 0x3fffffff 0x00000540   DPI pin sources
0x1c11 0x3fffffff 0x00000000
0x1c12 0x00ffffff 0x00000000
0x1c20 0x3fffffff 0x00000040   DPI pin output enables
0x1c21 0x3fffffff 0x00000040
0x1c22 0x00ffffff 0x00000000
```

So the audio pins are: in on DAI 6, 15, 18, 19; out on DAI 1, 2, 3, 20; clocks
out on 4, 9, 10, 11, 17; master clock in on 7.

- Domain A (fast): SPORT2 A receives pin 15, SPORT5 A transmits on pin 20,
  bit clock on pins 9 and 17, frame sync on pin 4.
- Domain B: SPORT0 A/B receive pins 18/19, SPORT1 A receives pin 6, SPORT4
  A/B transmit on pins 1/2, SPORT6 transmits (pin 3 ?), bit clock on pin 10,
  frame sync on pin 11.

Which domain is the codec and which is the LPC4350's I2S is not settled.

## Serial ports

All are clock and frame slaves (clocked by the PCG through the SRU), I2S mode,
32-bit words, DMA with chaining:

| port | control address | value | direction |
|---|---|---|---|
| SPORT0 | 0x0c00 | 0x013d29f1 | receive A and B |
| SPORT1 | 0x0c01 | 0x000d29f1 | receive A |
| SPORT2 | 0x0400 | 0x000d29f1 | receive A |
| SPORT4 | 0x0800 | 0x033d29f1 | transmit A and B |
| SPORT5 | 0x0801 | 0x020d29f1 | transmit A |
| SPORT6 | 0x4800 | 0x020d29f1 | transmit A |

The stock engine works in blocks of 16 samples.

## Boot, as the LPC4350 does it

What the stock application does on the wire.

| what | where |
|---|---|
| SPI | SSP0 (0x40083000): SCK P3_0 func 4, MISO P9_1 func 7, MOSI P9_2 func 7 |
| chip select | GPIO4[12] (P9_0), low for the whole download |
| SHARC reset | GPIO7[1] (PE_1), active low |
| line from the SHARC | GPIO7[0] (PE_0); the stock program pulls it low when it is up |

SSP0 runs in SPI mode 3 (CPOL = 1, CPHA = 1) with 16-bit frames at 20 MHz.
Each 32-bit word of the boot stream is sent **least significant bit first**: the
driver bit-reverses each 16-bit half and sends the low half first.

Sequence: reset low, wait, reset high, wait (the same delay value, 10000,
probably 10 ms each), chip select low, send the first 384 words (the kernel),
wait again, then the boot stream block by block, then poll GPIO7[0] until it
reads low (up to 10000 tries).

On the SHARC side the hardware loads those 384 words into 0x8c000..0x8c0ff,
raises the SPI interrupt (vector 0x8c030) and continues at 0x8c005. Further
words are received by writing the DMA address (0x1080, step 0x1081 = 1) and
then the count (0x1082); bit 15 of 0x1084 is set while a transfer runs. The
32-bit address of the instruction slot at 48-bit address A is
0x80000 + (A - 0x80000) * 3 / 2 (so 0x8c000 is 0x92000), and three 32-bit
words hold two instructions as one little-endian byte run.

FLAGS at start-up: FLAG2 and FLAG3 are outputs (and set), FLAG0 and FLAG1
inputs. The stock program has routines that set and clear FLAG3.

## Host link after boot

Two SPI ports are used with DMA, SPI (0x1000.., transmit) and SPI B (0x2800..,
receive), both with control 0x000c0000 while idle and status cleared with
0x56. The LPC4350 is the master. Message framing is in `README.md`.
