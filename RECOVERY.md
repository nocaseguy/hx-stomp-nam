# Switching between stock and custom firmware (safely)

The HX Stomp's NXP LPC4350 makes this low-risk: **custom firmware runs from RAM,
the stock firmware lives untouched in storage.** Sections 1 and 2 were tried on
the unit; the rest is read from the stock bootloader and updater and has not
been tried.

## The golden rule
**Prefer RAM (DFU) loading. It never writes flash, so a power-cycle always
restores stock.** A persistent install means writing the eMMC application
partition (section 4); leave the bootloader in the SPIFI NOR alone.

## 1. Stock -> custom (non-destructive, recommended)
1. Put the unit into the boot-ROM USB-DFU loader (see "Entering DFU" below).
2. `dfu-util -D build/firmware.dfu`  (loads into internal SRAM and runs).
3. To go back: **power-cycle.** The boot ROM boots stock from flash. Done.

Custom firmware never touched flash, so this is fully reversible with a power cycle.

## 2. Entering DFU
**Hold Page Right while powering the unit up.** It then enumerates as the NXP
boot ROM's DFU device; confirm with `dfu-util -l` (`Found DFU: [1fc9:000c]`).

Found on the unit:
- `dfu-util` on macOS works, on a direct port or through a hub. The end of
  every successful load reads `unable to read DFU status after completion`:
  the ROM has handed over to the image, which does not speak USB.
- The first DFU block must be a full 2048 bytes and the image header must be
  the `DA FF` form; `src/mkbootimg.py` does both. A short first block or the
  stock updater's `1A 00` header ends in `LIBUSB_ERROR_IO` at 0 bytes or an
  image that never starts.
- `dfu_reboot()` (src/dfu.c) takes a RAM image back to the DFU loader with no
  button: it leaves the text `BootIntoDfu!` at 0x1001FF00 and resets, and the
  stock bootloader in flash, which runs first after any reset, sees the text
  and starts the ROM's DFU loader. The unit is back on USB as 1fc9:000c a
  second or two later. This depends on the stock bootloader being in flash.
  A plain RGU core reset without the text boots stock in about 15 s.

## 3. Back to stock
Power-cycle. Nothing in this repository writes to the unit's storage: every
image is loaded into RAM by the boot ROM and is gone when the power goes.
