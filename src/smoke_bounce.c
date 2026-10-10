/* smoke_bounce.c - stage-0 proof of life that needs no display.
 *
 * Waits a few seconds and hands control back to the boot ROM's DFU loader
 * (dfu.c, the stock firmware's own routine). If the image runs, the unit
 * leaves USB when the download ends and comes back as 1fc9:000c by itself:
 *     dfu-util -D build/firmware-smoke-bounce.dfu; sleep 15; dfu-util -l */
#include "lpc4350.h"
#include "dfu.h"

int main(void) {
    for (volatile uint32_t n = 6000000; n; --n) { }
    dfu_reboot();
}
