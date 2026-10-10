/* nam_a2.h - Neural Amp Modeler "A2" WaveNet block (A2-Lite: 3 channels,
 * A2-Full: 8 channels), portable C, single precision.
 *
 * Architecture (fixed for every A2 model; only the channel count C differs):
 *   x[0]     = rechannel * in                               (1 -> C, no bias)
 *   for each of 23 layers l (kernel K_l, dilation d_l):
 *     a      = lrelu(conv_l(x[l]) + b_l + mixin_l * in, 0.01)
 *     head  += a
 *     x[l+1] = x[l] + W1x1_l a + b1x1_l                     (not after layer 22)
 *   out      = head_scale * (head_b + conv16(head))
 *
 * This is the straightforward one-sample-at-a-time reference: it is the
 * numerical ground truth for any optimised (tiled / fixed-point) kernel and is
 * what the host simulator runs. Mono in, mono out, 48 kHz models. */
#ifndef HX_DSP_NAM_A2_H
#define HX_DSP_NAM_A2_H
#include <stdint.h>
#include <stddef.h>

#define NAM_A2_LAYERS     23
#define NAM_A2_HEAD_TAPS  16
#define NAM_A2_MAX_CH     8

typedef struct {
    int      channels;                     /* 3 or 8 */
    const float *w;                        /* weight stream, .nam file order */
    /* offsets into w[] */
    uint32_t rechannel;
    uint32_t conv_w[NAM_A2_LAYERS], conv_b[NAM_A2_LAYERS];
    uint32_t mixin[NAM_A2_LAYERS], l1x1_w[NAM_A2_LAYERS], l1x1_b[NAM_A2_LAYERS];
    uint32_t head_w;
    float    head_b, head_scale;
    /* per-layer input history: ring of len[l] columns of C floats */
    float   *ring[NAM_A2_LAYERS];
    uint32_t len[NAM_A2_LAYERS], pos[NAM_A2_LAYERS];
    float    head[NAM_A2_HEAD_TAPS * NAM_A2_MAX_CH];
    uint32_t head_pos;
} nam_a2;

/* Number of floats in a model's weight stream / required state buffer. */
size_t nam_a2_weight_count(int channels);
size_t nam_a2_state_floats(int channels);

/* Bind a model. `weights` is the .nam "weights" array in file order (including
 * the trailing head_scale) and must outlive the block; `state` must hold
 * nam_a2_state_floats(channels) floats. Returns 0, or -1 on a bad channel count
 * or weight count. */
int   nam_a2_init(nam_a2 *m, int channels, const float *weights, size_t n_weights,
                  float *state);
void  nam_a2_reset(nam_a2 *m);
float nam_a2_tick(nam_a2 *m, float in);
void  nam_a2_process(nam_a2 *m, const float *in, float *out, uint32_t frames);

#endif
