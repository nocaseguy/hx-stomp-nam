/* smoke_thru.c - sound through our own code: what goes in comes out.
 *
 * Boots build/sharc_audio_thru.h (src/sharc/report.asm + audio.asm +
 * audio_thru.asm), which copies the inputs to the outputs through the codecs,
 * and switches the bypass relays so that the jacks carry that and not the
 * straight-through path (audio_pins.h). The screen shows the word the SHARC
 * sends once it is set up, 54485255 ("THRU") in green, then counts up about
 * once a second; after about five minutes the image goes back to the DFU
 * loader. */
#define SMOKE_SHARC_NO_MAIN
#include "smoke_sharc.c"
#include "../build/sharc_audio_thru.h"
#include "smoke_text.h"
#include "smoke_frames.h"
#include "audio_pins.h"
#include "dfu.h"

int main(void) {
    lcd_start();
    bands(0, 16, 0);
    audio_pins(0);
    link_init();
    sharc_boot(sharc_audio_thru, sizeof sharc_audio_thru / sizeof sharc_audio_thru[0]);

    text_start();
    uint32_t w = 0;
    for (unsigned n = 0; n < 3 && !frame_in(&w, 1); ++n) { }
    text_hex(0, 0, w, 8, w == SHARC_AUDIO_THRU_MAGIC ? 2 : 3);
    for (unsigned s = 0; s < 300; ++s) {
        text_hex(0, 2, s, 4, 1);
        spin(1000 * MS);
    }
    dfu_reboot();
}
