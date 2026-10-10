#include "nam_a2.h"

static const uint8_t k_kernel[NAM_A2_LAYERS] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6 };
static const uint8_t k_dilation[NAM_A2_LAYERS] = {
    1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239 };

#define SLOPE 0.01f

/* Columns of history layer l needs: its own lookback plus the current column. */
static uint32_t ring_len(int l) { return (uint32_t)(k_kernel[l] - 1) * k_dilation[l] + 1; }

size_t nam_a2_weight_count(int c) {
    size_t n = (size_t)c;                                   /* rechannel */
    for (int l = 0; l < NAM_A2_LAYERS; ++l)
        n += (size_t)c * c * k_kernel[l] + c                /* conv + bias */
           + c                                              /* mixin       */
           + (size_t)c * c + c;                             /* 1x1 + bias  */
    return n + (size_t)c * NAM_A2_HEAD_TAPS + 1 + 1;        /* head, bias, head_scale */
}

size_t nam_a2_state_floats(int c) {
    size_t n = 0;
    for (int l = 0; l < NAM_A2_LAYERS; ++l) n += ring_len(l);
    return n * (size_t)c;
}

void nam_a2_reset(nam_a2 *m) {
    for (int l = 0; l < NAM_A2_LAYERS; ++l) {
        for (uint32_t i = 0; i < m->len[l] * (uint32_t)m->channels; ++i) m->ring[l][i] = 0.0f;
        m->pos[l] = 0;
    }
    for (unsigned i = 0; i < NAM_A2_HEAD_TAPS * NAM_A2_MAX_CH; ++i) m->head[i] = 0.0f;
    m->head_pos = 0;
}

int nam_a2_init(nam_a2 *m, int c, const float *weights, size_t n_weights, float *state) {
    if ((c != 3 && c != 8) || n_weights != nam_a2_weight_count(c)) return -1;
    m->channels = c;
    m->w = weights;
    /* Stream order (NAM WaveNet): rechannel; per layer conv[i][j][k], conv bias,
     * mixin, 1x1[i][j], 1x1 bias; head[j][k], head bias; head_scale. */
    uint32_t o = 0;
    m->rechannel = o; o += (uint32_t)c;
    for (int l = 0; l < NAM_A2_LAYERS; ++l) {
        m->conv_w[l] = o; o += (uint32_t)(c * c * k_kernel[l]);
        m->conv_b[l] = o; o += (uint32_t)c;
        m->mixin[l]  = o; o += (uint32_t)c;
        m->l1x1_w[l] = o; o += (uint32_t)(c * c);
        m->l1x1_b[l] = o; o += (uint32_t)c;
        m->ring[l] = state;
        m->len[l]  = ring_len(l);
        state += m->len[l] * (uint32_t)c;
    }
    m->head_w = o; o += (uint32_t)(c * NAM_A2_HEAD_TAPS);
    m->head_b = weights[o++];
    m->head_scale = weights[o];
    nam_a2_reset(m);
    return 0;
}

float nam_a2_tick(nam_a2 *m, float in) {
    const int c = m->channels;
    const float *w = m->w;
    float x[NAM_A2_MAX_CH], a[NAM_A2_MAX_CH], hsum[NAM_A2_MAX_CH];

    for (int i = 0; i < c; ++i) { x[i] = w[m->rechannel + i] * in; hsum[i] = 0.0f; }

    for (int l = 0; l < NAM_A2_LAYERS; ++l) {
        const int K = k_kernel[l], d = k_dilation[l];
        const uint32_t len = m->len[l], pos = m->pos[l];
        float *ring = m->ring[l];
        for (int i = 0; i < c; ++i) ring[pos * (uint32_t)c + i] = x[i];

        const float *cw = w + m->conv_w[l];
        for (int i = 0; i < c; ++i) a[i] = w[m->conv_b[l] + i];
        for (int k = 0; k < K; ++k) {                       /* tap K-1 = newest */
            uint32_t back = (uint32_t)(K - 1 - k) * (uint32_t)d;
            const float *col = ring + ((pos + len - back) % len) * (uint32_t)c;
            for (int j = 0; j < c; ++j) {
                const float xv = col[j];
                for (int i = 0; i < c; ++i) a[i] += cw[(i * c + j) * K + k] * xv;
            }
        }
        for (int i = 0; i < c; ++i) {
            float v = a[i] + w[m->mixin[l] + i] * in;
            a[i] = v > v * SLOPE ? v : v * SLOPE;
            hsum[i] += a[i];
        }
        if (l + 1 < NAM_A2_LAYERS) {
            const float *lw = w + m->l1x1_w[l], *lb = w + m->l1x1_b[l];
            for (int i = 0; i < c; ++i) {
                float o = lb[i];
                for (int j = 0; j < c; ++j) o += lw[i * c + j] * a[j];
                x[i] += o;
            }
        }
        m->pos[l] = pos + 1 == len ? 0 : pos + 1;
    }

    uint32_t hp = m->head_pos;
    for (int i = 0; i < c; ++i) m->head[hp * NAM_A2_MAX_CH + i] = hsum[i];
    float y = m->head_b;
    const float *hw = w + m->head_w;
    for (int k = 0; k < NAM_A2_HEAD_TAPS; ++k) {            /* tap 15 = newest */
        const float *col = m->head +
            ((hp + NAM_A2_HEAD_TAPS - (uint32_t)(NAM_A2_HEAD_TAPS - 1 - k)) % NAM_A2_HEAD_TAPS) * NAM_A2_MAX_CH;
        for (int j = 0; j < c; ++j) y += hw[j * NAM_A2_HEAD_TAPS + k] * col[j];
    }
    m->head_pos = (hp + 1) % NAM_A2_HEAD_TAPS;
    return y * m->head_scale;
}

void nam_a2_process(nam_a2 *m, const float *in, float *out, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) out[i] = nam_a2_tick(m, in[i]);
}
