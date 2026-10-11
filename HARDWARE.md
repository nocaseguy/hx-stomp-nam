# HX Stomp hardware map

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


Derived by reverse-engineering the stock firmware's own bring-up code (clock,
SDRAM, pin mux, GPIO, display) plus public teardowns. **Confirmed** = read from
the firmware bytes; **inferred** = derived/standard value to verify on hardware.

## Processors
| part | role | notes |
|---|---|---|
| NXP LPC4350 (Cortex-M4F + M0) | control, UI, USB, I/O | flashless; XIP from QSPI NOR; runs app from SDRAM |
| ADI ADSP-21469 SHARC @450 MHz | audio DSP | boots via SPI/SPORT from the M4 (SSP link) |
| Cirrus CS4272 | stereo audio codec | 24-bit, I2S data + I2C control |

## Clock (confirmed)
- 12 MHz crystal -> CGU PLL1 x17 -> **204 MHz** BASE_M4_CLK.
- CGU 0x40050000: PLL1_STAT +0x40 (bit0 lock), PLL1_CTRL +0x44, BASE_M4_CLK +0x6C.
- EMC branch clock via CCU1 (+0x460/+0x468); CREG6 (0x4004312C) EMC clock divider.

## External SDRAM (read from the stock bootloader and updater; works on the unit, 2026-10-10)
The bootloader keeps its times in nanoseconds in a table; the updater has the
same routine with the numbers worked out. `src/sdram.h` follows it.
- EMC 0x40005000, DYCS0 -> **0x28000000, 32 MB**: two 128 Mbit x16 parts on a
  32-bit data bus (DynamicConfig0 0x4480, then 0x84480 with the buffers on;
  D0-D31 and four DQM pins are routed). RAS 2, CAS 2 (0x202).
- Clock: stock takes the core to **204 MHz** first (12 MHz crystal, PLL1 x17,
  first at 102 MHz then direct) and runs the EMC at half, 102 MHz: CREG6 bit 16
  set, CCU1 CLK_M4_EMCDIV_CFG (0x40051478) = 0x21, EMCDELAYCLK (0x40086D00) =
  0x5555. The updater, loaded by the ROM's DFU as our images are, measures the
  core clock (CGU FREQ_MON) and sets 204 MHz unless it finds it: what the ROM
  leaves is not said anywhere.
- Pins (pad value: 0xF0 = no pull-up, fast, input on, no filter, plus the
  function): CLK0-3 = F5 F0 F5 F0; P1_7-14 F3 (D0-7); P5_4-7, P5_0-3 F2 (D8-15);
  PD_2-9 F2 (D16-23); PE_5-12 F3 (D24-31); P2_9-13 F3 (A0-4); P1_0-2 F2 (A5-7);
  P2_8 F3, P2_7 F3, P2_6 F2 (A8-10); P2_2, P2_1, P2_0 F2 (A11-13); P6_8 F1 (A14);
  P1_6 F3 (WE); P6_4, P6_5 F3 (CAS, RAS); P6_10-12 F3 (DQM1, CKE, DQM0); PE_13 F3
  (DQM3); PD_0 F2 (DQM2); P6_9 F3 (DYCS0). Which signal each pin carries is
  from the LPC4350's manual, not from the board.
- Times in ns, registers +0x30 to +0x58: tRP 18, tRAS 44, tSREX 74, tAPR 10,
  tDAL 44 (+1), tWR 22, tRC 62, tRFC 62, tXSR 74, tRRD 14, tMRD 22; each
  register = ns x EMC MHz / 1000, cut to a whole number (at 102 MHz: 1 4 7 1 5
  2 6 6 7 1 2). Refresh (+0x24) = (15625 x MHz / 1000 + 15) / 16 = 100.
  ReadConfig (+0x28) = 1.
- Order: Control 0, clock and pins, Control 1, Config0, RasCas, ReadConfig,
  times, refresh, NOP (0x183), wait, precharge all (0x103), wait, MODE (0x083),
  a byte read at 0x28044000 (mode 0x22 << 13: CAS 2, bursts of 4), wait,
  normal (0x003), Config0 with bit 19.
- Just before, both programs make P6_7 GPIO5[15] and drive it low; why is not
  known.
- `src/init.c`'s set-up (the main firmware's) is none of this and would not
  work: 16-bit bus, wrong pins, wrong clock select bit.
- `src/sdram.h` differs from stock in one thing: it leaves the core clock
  alone, measures it, runs the EMC at half of it and works the times out for
  that. On the unit (`make smoke-sdram`): **the boot ROM's DFU leaves the core
  at 96 MHz** (measured against the IRC), so the EMC runs at 48 MHz; the
  set-up goes through and 768 KB at 0x28000000 pass a write and read-back
  test, twice over with the words turned. The image's size probe saw no
  repeat of address 0 up to 64 MB, which does not fit two 128 Mbit parts and
  is not explained. A write 128 MB up (the next chip select) stopped the
  processor: don't.
- The screen runs at 16 bits a pixel from there (`src/screen16.h`: LCD_CTRL
  bpp 6, bit 8 set; colours come out as named), two buffers shown turn about;
  `src/stomp.c` draws about 20 frames a second. **Turning the LCD controller
  off (LcdEn) leaves the panel white** even when it is turned on again with
  the settings that worked; sending the panel its set-up sequence again
  (`lcd_panel_setup()`, `lcd_panel_on()`) brings the picture back.

## Display (working on the unit: `src/smoke_lcd.c`)
- **LPC4350 built-in LCD controller (LCDC) @ 0x40008000**, parallel RGB, TFT.
- **240 x 320** as the controller scans it (PPL 240, LPP 320); HBP/HFP/HSW
  20/10/10, vertical 2/4/2, IPC+IHS+IVS inverted. Stock runs 16 bpp 5:6:5 at
  70 Hz from PLL1; the smoke image runs 4 bpp from the 12 MHz IRC.
  (An earlier note here said 480 x 320, inferred from a buffer size: wrong.)
- The panel has an **ILI9341-style controller that must be configured over
  SSP1** before it shows anything: 9-bit frames (bit 8 = data), mode 0, 1 MHz,
  SSEL P1_5, SCK P1_19, MISO P0_0, MOSI P0_1. The sequence is in
  `smoke_lcd.c`, the commands and values the bootloader sends; it ends with sleep-out, a pause,
  display-on.
- **Backlight: GPIO7[14] (PE_14), active low.** Stock drives it high before
  set-up and low afterwards.
- **GPIO6[28] (PD_14) must be driven low** or the screen stays dark; stock
  does it before anything else. What it switches is not known.
- Stock framebuffers in SDRAM: 0x29F00CA8 and 0x29F4BCA8.
- `src/display.c` (the main firmware's driver) uses the same pins, timings,
  panel set-up and backlight (`src/lcd_panel.h`) and presents the screen as it
  is read, 320 x 240. Its own parts - pixel clock from PLL1, 16 bpp, the
  framebuffer in SDRAM, colour order - have not run on the unit.

## Controls: switches, knobs, buttons, LEDs (mechanism confirmed)
Recovered from the stock **factory test mode** ("Button Test", "Pedal Test",
"LED/Footswitch", "Encoder/Volpot", "Footswitch 1-8") and a decoded GPIO HAL
register table (DIR/PIN/SET/CLR for all 8 ports).

| control | mechanism | confidence |
|---|---|---|
| footswitches, buttons, knobs, volume | reported by the footswitch board's controller over I2C; decoded, see "Front panel" below | confirmed on the unit |
| footswitch LEDs | direct GPIO SET/CLR (0x400F6200/6280) | confirmed |
| rotary encoder knob(s) | polled GPIO A/B quadrature, CT-timer tick (0x400C3000) | high (no pin-ISR path exists) |
| volume pot + EXP pedals | polled ADC0 (0x400E3000) + ADC1 (0x400E4000) | confirmed |
| SSP0 | SHARC boot/comms link: mode 3, 16-bit frames, CS GPIO4[12], reset GPIO7[1] | confirmed |

No shift registers (74HC165/595) and no SGPIO: those bases have zero code refs.
Only the concrete port/pin per control and the ADC channel numbers need the
schematic; the read/drive mechanism is settled. See src/input/hx_pinmap.h.

## Pin mux / GPIO (ports confirmed; per-pin function inferred)
- SCU 0x40086000. EMC bus pins use the recommended EMC pad config (EZI+ZIF+EHS,
  pull-up off). I2C1 uses the dedicated open-drain I2C pads (SFSI2C0 0x40086C0C).
- SCU ports touched at boot: P0,P2,P3,P4,P5,P6,P7,P8,P10,P11,P13,P14,P15.
  P4/P7 cluster next to LCD init -> LCD data/sync pins (inferred).
- GPIO 0x400F4000: a boot-time output block configures LEDs, true-bypass relays,
  display reset/backlight, and SHARC reset/boot control (inferred roles).
  Footswitches are GPIO inputs. Per-pin roles need the schematic.

## Audio path, as found on the unit (see also tools/sharc/HARDWARE.md)
- **Bypass relays: GPIO6[25] (PD_11) and GPIO6[27] (PD_13)**, mode 0x14. At
  rest (pins not driven) the input jack is wired straight to the main output:
  a guitar is heard there with nothing else running. Each pin driven high
  clicks a relay, and with both high the straight-through path is gone.
- GPIO7[0] follows the SHARC's FLAG3 (not FLAG2).
- The stock application's nine "audio" outputs (GPIO5[17], 6[24], 5[18],
  6[0], 6[2], 7[23], 5[19], 0[10], 7[15]) and the 10 ms low pulse on GPIO5[17]
  are in `src/smoke_audio.c`; what each does is not known. GPIO5[18], 6[0]
  and 6[2] are written together from one value in several places (input
  impedance select?).
- **The footswitch board's controller is at 7-bit address 0x2A on I2C bus 0**
  (it acknowledges there; bus 1 has devices at 0x58 and 0x59). The stock
  footswitch-light message works: 8 bytes {8, ~sum of the next six, 0, switch,
  0, red, green, blue 0-100} lit footswitch 1's ring blue. The controller
  writes to the LPC4350 as a slave (own address register 0x80) when a control
  changes; see "Front panel" below. Light messages sent 50 ms apart were
  dropped (only the first took); a quarter of a second apart all arrive.
- With nothing connected to them, ADC0 channels 1 and 2 read about 0x360 and
  0x370 and the rest full scale; ADC1 reads the same pins.
- The LPC4350 images must enable the FPU (CPACR) before any float arithmetic:
  `startup.c` does. Without it `pedal.c` stopped at its first number.

## Front panel (measured on an HX Stomp, 2026-10-10)
Every control comes from the footswitch board's controller as a 6-byte write
to the LPC4350 on I2C bus 0: `06, check, kind, 00, control, value`, where the
check byte is the inverted sum of the four that follow it.

| kind | meaning | value |
|---|---|---|
| 00 | a switch | 01 down or touched, 00 up or let go |
| 01 | a knob turned | steps since the last message, signed (01 clockwise, FF back; FE, 02 when fast) |
| 02 | the volume knob, control 00 | position 00-64 (0-100); only sent when it moves |
| 03 | unknown: on some runs only, value near 24, drifting by one | |

| control | kind 00 (switch) | kind 01 (turn) |
|---|---|---|
| left, middle, right knob under the screen | 00, 01, 02 | 00, 01, 02 |
| upper knob, lower knob | 03, 04 | 03, 04 |
| PAGE left, VIEW, ACTION, PAGE right | 05, 06, 07, 08 | |
| footswitch 1, 2, 3 pressed | 09, 0A, 0B | |
| footswitch 1, 2, 3 touched | 0C, 0D, 0E | |

A footswitch press arrives as touch down, press down, press up, touch up. The
position of the volume knob at power-up is not sent unasked; how to ask is
not known yet. `src/pedal.c` uses all of this; `src/usb_mon.h` with
`tools/usbmon.py` streams the messages to a computer over USB and takes the
image back to the DFU loader without the Page Right hold.

## Other peripherals (confirmed present)
| peripheral | base | use |
|---|---|---|
| I2S0 | 0x400A2000 | audio link |
| I2C1 | 0x400E0000 | codec + EEPROM control |
| SSP0 | 0x40083000 | SHARC boot and host link (see tools/sharc/HARDWARE.md) |
| SSP1 | 0x400C5000 | second SPI device |
| ADC1 | 0x400E4000 | expression pedal / analog in |
| USART0/2/3, UART1 | 0x4008xxxx | MIDI (USART3 heaviest) + debug |
| USB0 | 0x40006000 | USB audio/MIDI + DFU |
| SPIFI | 0x40003000 | QSPI NOR boot flash (XIP 0x14000000) |
