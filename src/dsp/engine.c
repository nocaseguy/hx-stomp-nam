#include "engine.h"
#include <math.h>

#define Q24  8388608.0f    /* 2^23 */

void engine_init(audio_engine *e, float fs) {
    e->fs = fs; e->gain = 1.0f; e->nam = 0;
    /* A simple guitar-ish EQ: high-pass rumble filter, low-mid cut, presence peak. */
    biquad_highpass(&e->c[0], fs, 80.0f,  0.7071f);
    biquad_peaking (&e->c[1], fs, 400.0f, 1.0f, -3.0f);
    biquad_peaking (&e->c[2], fs, 3000.0f,1.2f, +4.0f);
    for (int i = 0; i < ENGINE_STAGES; ++i) { biquad_reset(&e->sL[i]); biquad_reset(&e->sR[i]); }
}

void engine_set_gain_db(audio_engine *e, float db) { e->gain = powf(10.0f, db/20.0f); }
void engine_set_nam(audio_engine *e, nam_a2 *nam) { e->nam = nam; }

static inline float clip1(float x){ return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x); }

void engine_process_i32(audio_engine *e, int32_t *lr, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        float l = (float)lr[2*i]   / Q24;
        float r = (float)lr[2*i+1] / Q24;
        if (e->nam) l = r = nam_a2_tick(e->nam, l);
        for (int s = 0; s < ENGINE_STAGES; ++s) {
            l = biquad_tick(&e->c[s], &e->sL[s], l);
            r = biquad_tick(&e->c[s], &e->sR[s], r);
        }
        l = clip1(l * e->gain); r = clip1(r * e->gain);
        lr[2*i]   = (int32_t)(l * Q24);
        lr[2*i+1] = (int32_t)(r * Q24);
    }
}
