/* biquad.h - single-precision biquad IIR filter (Direct Form I, Transposed II).
 * Suitable for the LPC4350 Cortex-M4F (hardware single-precision FPU) or as a
 * reference for a SHARC port. Coefficients use the Audio-EQ-Cookbook forms. */
#ifndef HX_DSP_BIQUAD_H
#define HX_DSP_BIQUAD_H
#include <stdint.h>

typedef struct { float b0, b1, b2, a1, a2; } biquad_coeffs;
typedef struct { float z1, z2; } biquad_state;   /* Transposed Direct Form II */

/* Design helpers. fs = sample rate (Hz), f0 = corner (Hz), Q, gain in dB. */
void biquad_lowpass (biquad_coeffs *c, float fs, float f0, float q);
void biquad_highpass(biquad_coeffs *c, float fs, float f0, float q);
void biquad_peaking (biquad_coeffs *c, float fs, float f0, float q, float gain_db);

static inline void biquad_reset(biquad_state *s) { s->z1 = s->z2 = 0.0f; }

/* Process one sample (TDF-II: 2 states, 5 mults). */
static inline float biquad_tick(const biquad_coeffs *c, biquad_state *s, float x) {
    float y  = c->b0 * x + s->z1;
    s->z1 = c->b1 * x - c->a1 * y + s->z2;
    s->z2 = c->b2 * x - c->a2 * y;
    return y;
}

/* Process a block in place. */
void biquad_process(const biquad_coeffs *c, biquad_state *s, float *buf, uint32_t n);

#endif
