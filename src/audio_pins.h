/* audio_pins.h - what the LPC4350 has to do for sound to pass on the HX Stomp
 * XL. Include after lpc4350.h. Everything else about the audio hardware is
 * the SHARC's business (src/sharc/audio.asm).
 *
 * Found on the unit:
 *   - GPIO6[25] (PD_11) and GPIO6[27] (PD_13) are the bypass relays. At rest
 *     the input jack is wired straight to the main output; both high, the
 *     jacks carry what the codecs take in and put out.
 * Read from the stock application, and set here the way it first sets them;
 * what each one does, and which of them matter, is not known:
 *   - its board set-up makes ten more outputs with the relays
 *     and leaves them low
 *   - its audio set-up sets nine: GPIO5[17] and GPIO6[24] high,
 *     the rest low (GPIO5[18], 6[0], 6[2] are written together elsewhere:
 *     input impedance?)
 *   - the bootloader and the application drive GPIO4[8], 4[9] and 5[15] low
 *     from the start and never change them
 * Not needed, tried on the unit: the application's 10 ms low pulse on
 * GPIO5[17], which is not the codecs' reset. */
struct audio_pin { uint8_t sp, spin, mode, port, pin, level; };

static const struct audio_pin audio_pin_table[] = {
    {13, 11, 0x14, 6, 25, 1}, {13, 13, 0x14, 6, 27, 1},     /* the relays: out of bypass */
    {15, 1, 0x04, 7, 16, 0}, {4, 0, 0x00, 2, 0, 0},   {1, 16, 0x00, 0, 3, 0},
    {6, 1, 0x00, 3, 0, 0},   {1, 4, 0x00, 0, 11, 0},  {14, 2, 0x04, 7, 2, 0},
    {14, 3, 0x04, 7, 3, 0},  {13, 16, 0x04, 6, 30, 0}, {13, 15, 0x04, 6, 29, 0},
    {14, 4, 0x14, 7, 4, 0},
    {9, 4, 0x34, 5, 17, 1},  {13, 10, 0x34, 6, 24, 1}, {9, 5, 0x14, 5, 18, 0},
    {12, 1, 0x14, 6, 0, 0},  {12, 3, 0x14, 6, 2, 0},   {15, 9, 0x14, 7, 23, 0},
    {10, 4, 0x14, 5, 19, 0}, {1, 3, 0x10, 0, 10, 0},   {14, 15, 0x14, 7, 15, 0},
    {10, 1, 0x30, 4, 8, 0},  {10, 2, 0x30, 4, 9, 0},   {6, 7, 0x14, 5, 15, 0},
};

/* `bypass`: leave the relays at rest, so the jacks stay wired straight through. */
static void audio_pins(int bypass) {
    for (unsigned i = 0; i < sizeof audio_pin_table / sizeof audio_pin_table[0]; ++i) {
        const struct audio_pin *p = &audio_pin_table[i];
        unsigned level = i < 2 && bypass ? 0 : p->level;
        SCU_SFS(p->sp, p->spin) = p->mode;
        if (level) GPIO_SET(p->port) = 1u << p->pin; else GPIO_CLR(p->port) = 1u << p->pin;
        GPIO_DIR(p->port) |= 1u << p->pin;
    }
}
