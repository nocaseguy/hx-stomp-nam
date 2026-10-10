#include "dfu.h"
#include "lpc4350.h"
/* Back to the boot ROM's USB DFU loader, the way the stock firmware does it.
 *
 * The ROM only enters DFU by itself when Page Right is held at power-up (a
 * boot strap). The software route goes through the stock bootloader in flash:
 * the application leaves the text "BootIntoDfu!" at 0x1001FF00 and resets the
 * chip; RAM survives the reset, the ROM boots the bootloader as usual, and
 * the bootloader's first act is to look for that text, clear it
 * and start the ROM's DFU loader. Read from the stock application
 * and bootloader. Works on the unit.
 *
 * 0x1001FF00 is 256 bytes under the top of the RAM the smoke images use for
 * their stack, so call this from near the top of main, not from deep down. */
#define RGU_RESET_CTRL0  REG32(RGU_BASE+0x100)
void dfu_reboot(void){
    static const char magic[13] = "BootIntoDfu!";
    volatile char *p = (volatile char *)0x1001FF00u;
    for (unsigned i = 0; i < sizeof magic; ++i) p[i] = magic[i];
    SCU_SFS(2,9) = 0;      /* the stock routine also puts the four boot pins back */
    SCU_SFS(2,8) = 0;      /* to their reset state: P2_9, P2_8, P1_2, P1_1        */
    SCU_SFS(1,2) = 0;
    SCU_SFS(1,1) = 0;
    for (volatile int i=0;i<0x5FB4;i++){ }
    RGU_RESET_CTRL0 = 1;                        /* CORE_RST */
    for (;;){ }
}
