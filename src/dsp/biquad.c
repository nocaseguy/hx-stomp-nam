#include "biquad.h"
#include <math.h>

static void norm(biquad_coeffs *c, float b0, float b1, float b2,
                 float a0, float a1, float a2) {
    c->b0 = b0 / a0; c->b1 = b1 / a0; c->b2 = b2 / a0;
    c->a1 = a1 / a0; c->a2 = a2 / a0;
}

void biquad_lowpass(biquad_coeffs *c, float fs, float f0, float q) {
    float w = 2.0f * (float)M_PI * f0 / fs;
    float cw = cosf(w), sw = sinf(w), al = sw / (2.0f * q);
    norm(c, (1-cw)/2, 1-cw, (1-cw)/2, 1+al, -2*cw, 1-al);
}
void biquad_highpass(biquad_coeffs *c, float fs, float f0, float q) {
    float w = 2.0f * (float)M_PI * f0 / fs;
    float cw = cosf(w), sw = sinf(w), al = sw / (2.0f * q);
    norm(c, (1+cw)/2, -(1+cw), (1+cw)/2, 1+al, -2*cw, 1-al);
}
void biquad_peaking(biquad_coeffs *c, float fs, float f0, float q, float gain_db) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w = 2.0f * (float)M_PI * f0 / fs;
    float cw = cosf(w), sw = sinf(w), al = sw / (2.0f * q);
    norm(c, 1+al*A, -2*cw, 1-al*A, 1+al/A, -2*cw, 1-al/A);
}
void biquad_process(const biquad_coeffs *c, biquad_state *s, float *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) buf[i] = biquad_tick(c, s, buf[i]);
}
