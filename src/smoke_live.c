/* smoke_live.c - play through the NAM model.
 *
 * Boots build/sharc_nam_live.h (nam_a2_simd.asm + report.asm + audio.asm +
 * nam_live.asm with an 8-channel model) and switches the bypass relays
 * (audio_pins.h): what goes into the inputs comes out of every output
 * through the model, 12 dB down. The screen shows what the SHARC sends once
 * it is set up: 4C495645 ("LIVE") in green and the offset between its
 * transmit and receive rings, then a count of seconds. After half an hour
 * the image goes back to the DFU loader; power off to stop sooner. */
#define SMOKE_SHARC_NO_MAIN
#include "smoke_sharc.c"
#include "../build/sharc_nam_live.h"
#include "smoke_text.h"
#include "smoke_frames.h"
#include "audio_pins.h"
#include "dfu.h"

int main(void) {
    lcd_start();
    bands(0, 16, 0);
    audio_pins(0);
    link_init();
    sharc_boot(sharc_nam_live, sizeof sharc_nam_live / sizeof sharc_nam_live[0]);

    text_start();
    uint32_t w[2] = {0, 0};
    for (unsigned n = 0; n < 4 && !frame_in(w, 2); ++n) { }
    text_hex(0, 0, w[0], 8, w[0] == SHARC_NAM_LIVE_MAGIC ? 2 : 3);
    text_hex(10, 0, w[1], 8, 1);
    for (unsigned s = 0; s < 1800; ++s) {
        text_hex(0, 2, s, 4, 1);
        spin(1000 * MS);
    }
    dfu_reboot();
}
