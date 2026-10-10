/* tele.h - one small number back to the host from a RAM image, with no display
 * and nobody watching: wait that many seconds, then return to the DFU loader.
 * tools/tele.py loads the image and times how long the unit is off USB
 * (about 0.54 s a count on the unit).
 * Include after dfu.h and something that provides spin() and MS. */
__attribute__((noreturn)) static void tele(unsigned value) {
    for (unsigned i = 0; i < value; ++i) spin(1000 * MS);
    dfu_reboot();
}
