/* dfu.h - reboot the LPC4350 into the ROM USB-DFU loader (software switch).
 * Lets custom firmware hand control back to the boot ROM's DFU loader so you can
 * load a different image over USB, without opening the unit. A power-cycle from
 * DFU boots the untouched stock firmware from flash. */
#ifndef HX_DFU_H
#define HX_DFU_H
void dfu_reboot(void) __attribute__((noreturn));   /* does not return */
#endif
