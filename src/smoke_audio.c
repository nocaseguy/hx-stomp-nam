/* smoke_audio.c - the audio hardware, measured from the SHARC.
 *
 * Boots build/sharc_audio_probe.h (src/sharc/report.asm + audio.asm +
 * audio_probe.asm) as smoke_sharc.c boots hello, switches the bypass relays
 * (audio_pins.h) and shows what the probe reports, every second or two, as
 * hex text (smoke_text.h, smoke_frames.h):
 *   row 0      edges seen (green); times the line stood still (red); frames read
 *   row 1      magic, green when it is right
 *   rows 2-3   words received in the count: SPORT0 A, SPORT0 B / SPORT1 A, SPORT2 A
 *   rows 4-5   the largest |word| on each, in the same order
 *   row 6      core cycles the count took, then the sample rate that makes of
 *              SPORT0 A in Hz (hex), taking the core to be at 450 MHz and two
 *              words a frame
 * Play into the inputs: the largest values of the port that carries them rise.
 * The probe also beeps on every output. After about three minutes the image
 * goes back to the DFU loader. */
#define SMOKE_SHARC_NO_MAIN
#include "smoke_sharc.c"
#include "../build/sharc_audio_probe.h"
#include "smoke_text.h"
#include "smoke_frames.h"
#include "audio_pins.h"
#include "dfu.h"

#define WORDS SHARC_AUDIO_PROBE_WORDS

int main(void) {
    lcd_start();
    bands(0, 16, 0);
    audio_pins(0);
    link_init();
    sharc_boot(sharc_audio_probe, sizeof sharc_audio_probe / sizeof sharc_audio_probe[0]);

    text_start();
    counts();
    static uint32_t w[WORDS];
    for (unsigned frames = 0; frames < 120 && stuck < 40;) {
        int got = frame_in(w, WORDS);
        counts();
        if (!got) continue;
        text_hex(10, 0, ++frames, 2, 1);
        text_hex(0, 1, w[0], 8, w[0] == SHARC_AUDIO_PROBE_MAGIC ? 2 : 4);
        for (unsigned i = 1; i < WORDS; ++i)
            text_hex((i - 1) % 2 * 10, 2 + (i - 1) / 2, w[i], 8, 1);
        if (w[9])
            text_hex(10, 6, (uint32_t)((uint64_t)w[1] * 225000000u / w[9]), 8, 4);
    }
    dfu_reboot();
}
