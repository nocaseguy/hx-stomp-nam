/* Host test for the NAM A2 block.
 *   test_nam_a2                      self-checks only
 *   test_nam_a2 in.f32 ref.f32       also compare against a reference render
 *   test_nam_a2 in.f32 ref.f32 out.f32   ...and write our output
 * The .f32 files are raw mono little-endian float32 at 48 kHz. The reference
 * comes from NeuralAmpModelerCore's `render` tool (generic WaveNet path), which
 * prewarms the model with silence before the first sample; we do the same. */
#include "../src/dsp/nam_a2.h"
#include "nam_a2_model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static float *load(const char *p, long *n) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); *n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    float *b = malloc((size_t)*n * 4);
    if (fread(b, 4, (size_t)*n, f) != (size_t)*n) { perror(p); exit(2); }
    fclose(f); return b;
}

int main(int argc, char **argv) {
    int fail = 0;
    const int C = NAM_MODEL_CHANNELS;
    float *state = malloc(nam_a2_state_floats(C) * sizeof(float));
    nam_a2 m;

    if (nam_a2_init(&m, C, nam_model_weights, NAM_MODEL_COUNT, state) != 0) {
        printf("FAIL init rejected the model (%d ch, %d weights, expected %zu)\n",
               C, NAM_MODEL_COUNT, nam_a2_weight_count(C));
        return 1;
    }
    printf("ok   model bound: %d channels, %d weights, %zu state floats\n",
           C, NAM_MODEL_COUNT, nam_a2_state_floats(C));
    if (nam_a2_init(&m, C, nam_model_weights, NAM_MODEL_COUNT - 1, state) == 0) {
        printf("FAIL init accepted a short weight stream\n"); fail = 1;
    } else printf("ok   short weight stream rejected\n");
    nam_a2_init(&m, C, nam_model_weights, NAM_MODEL_COUNT, state);

    /* Silence settles to a constant (the model's DC offset), finite output. */
    float y = 0, prev = 0;
    for (int i = 0; i < 8192; ++i) { prev = y; y = nam_a2_tick(&m, 0.0f); }
    if (!isfinite(y) || fabsf(y - prev) > 1e-7f) { printf("FAIL silence did not settle (%g, %g)\n", prev, y); fail = 1; }
    else printf("ok   silence settles (dc = %g)\n", y);

    /* A sine in gives a bounded, non-trivial, non-linear response. */
    double e_in = 0, e_out = 0; float peak = 0;
    for (int i = 0; i < 48000; ++i) {
        float x = 0.25f * sinf(2.0f * (float)M_PI * 220.0f * (float)i / 48000.0f);
        float o = nam_a2_tick(&m, x);
        if (!isfinite(o)) { printf("FAIL non-finite output at %d\n", i); return 1; }
        e_in += x * x; e_out += o * o; if (fabsf(o) > peak) peak = fabsf(o);
    }
    if (e_out < 1e-6 || peak > 4.0f) { printf("FAIL sine response implausible (rms %g, peak %g)\n", sqrt(e_out / 48000), peak); fail = 1; }
    else printf("ok   220 Hz sine: in rms %.4f -> out rms %.4f, peak %.4f\n", sqrt(e_in / 48000), sqrt(e_out / 48000), peak);

    if (argc >= 3) {
        long n, nr; float *in = load(argv[1], &n), *ref = load(argv[2], &nr);
        if (nr < n) n = nr;
        nam_a2_reset(&m);
        for (int i = 0; i < 8192; ++i) nam_a2_tick(&m, 0.0f);       /* prewarm */
        float *out = malloc((size_t)n * 4);
        nam_a2_process(&m, in, out, (uint32_t)n);
        double err = 0, sig = 0; float maxd = 0;
        for (long i = 0; i < n; ++i) {
            float d = fabsf(out[i] - ref[i]);
            if (d > maxd) maxd = d;
            err += (double)d * d; sig += (double)ref[i] * ref[i];
        }
        double snr = 10 * log10(sig / (err + 1e-30));
        if (maxd > 1e-4f) { printf("FAIL vs reference: max |diff| %g over %ld samples (SNR %.1f dB)\n", maxd, n, snr); fail = 1; }
        else printf("ok   matches reference render: max |diff| %.3g over %ld samples (SNR %.1f dB)\n", maxd, n, snr);
        if (argc >= 4) { FILE *f = fopen(argv[3], "wb"); fwrite(out, 4, (size_t)n, f); fclose(f); }
    } else printf("note no reference given; skipped the comparison against NAM Core\n");

    printf(fail ? "\nNAM A2 TEST FAILURES\n" : "\nALL NAM A2 TESTS PASS\n");
    return fail;
}
