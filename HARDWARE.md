# HX Stomp XL hardware map

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

## External SDRAM (register map + sequence confirmed; timings standard)
- EMC 0x40005000, DYCS0 -> **0x28000000**, size >=32 MB (256 Mbit x16).
- Dynamic regs: Control +0x20, Refresh +0x24, ReadConfig +0x28, tRP/tRAS/... +0x30..+0x50,
  Config0 +0x100, RasCas0 +0x104.
- Init: NOP (0x183) -> PALL (0x103) -> MODE (0x083) -> NORMAL (0x000), with an
  init-refresh window; mode register loaded by a read at 0x28000000 + (mode<<shift).
- Exact timing values live in a data table not traced; `src/init.c` uses standard
  LPC4350 32 MB values (TUNE to the actual part).

## Display (working on the unit: `src/smoke_lcd.c`)
- **LPC4350 built-in LCD controller (LCDC) @ 0x40008000**, parallel RGB, TFT.
- **240 x 320** as the controller scans it (PPL 240, LPP 320); HBP/HFP/HSW
  20/10/10, vertical 2/4/2, IPC+IHS+IVS inverted. Stock runs 16 bpp 5:6:5 at
  70 Hz from PLL1; the smoke image runs 4 bpp from the 12 MHz IRC.
  (An earlier note here said 480 x 320, inferred from a buffer size: wrong.)
- The panel has an **ILI9341-style controller that must be configured over
  SSP1** before it shows anything: 9-bit frames (bit 8 = data), mode 0, 1 MHz,
  SSEL P1_5, SCK P1_19, MISO P0_0, MOSI P0_1. The sequence is in
  `smoke_lcd.c`, copied from the bootloader; it ends with sleep-out, a pause,
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
| 8 footswitches + buttons | direct GPIO PIN read (0x400F6100..611C), active-low | confirmed |
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
- The stock I2C devices, bus 0 address 0x2A and bus 1 address 0x40, are
  driven as message queues and do not look like converters.

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
