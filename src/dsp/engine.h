/* engine.h - minimal stereo audio engine for the HX Stomp control MCU.
 * Converts 24-bit codec samples (in 32-bit I2S slots) to float, runs a small
 * biquad EQ chain + gain, and converts back with clipping. This is the block
 * callback you would wire to the I2S/DMA RX-TX on the real hardware. */
#ifndef HX_DSP_ENGINE_H
#define HX_DSP_ENGINE_H
#include <stdint.h>
#include "biquad.h"
#include "nam_a2.h"

#define ENGINE_STAGES 3

typedef struct {
    float fs;
    float gain;                         /* linear output gain */
    biquad_coeffs c[ENGINE_STAGES];
    biquad_state  sL[ENGINE_STAGES], sR[ENGINE_STAGES];
    nam_a2 *nam;                        /* optional amp model, ahead of the EQ */
} audio_engine;

void engine_init(audio_engine *e, float fs);
void engine_set_gain_db(audio_engine *e, float db);
/* Insert (or remove, with NULL) a NAM A2 block. The model is mono: it is fed
 * the left input and its output drives both channels. */
void engine_set_nam(audio_engine *e, nam_a2 *nam);

/* Interleaved L,R 24-bit samples (sign-extended in int32), n frames. In place. */
void engine_process_i32(audio_engine *e, int32_t *lr, uint32_t frames);

#endif
