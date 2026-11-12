/*
 * The LXR voice filter for all voices at once, in NEON lanes.
 *
 * The zero-delay-feedback state-variable filter of ResonantFilter.c, LXR
 * firmware, Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * The filter is a per-sample recursion, so one voice cannot be vectorized
 * over time. Instead each NEON lane runs one voice; two groups of four run
 * interleaved so one group's latency hides the other's. The two-pole mode
 * and the off and zero settings run through the original scalar code.
 */
#include "filter_bank.h"
#include "lxr_neon.h"
#include <string.h>

#define LANES               4u
#define GROUPS              2u
#define BANK_VOICES         (LANES * GROUPS)
#define INPUT_SCALE         (1.0f / 32767.0f)
#define OUTPUT_GAIN         32767.0f            /* FILTER_GAIN */
#define STABLE_F_LIMIT      0.4499f             /* above it the resonance is forced off */
#define TANH_LIMIT          1.95f
#define TANH_NUMERATOR      4.15f
#define TANH_DENOMINATOR    4.29f

/* Pade approximant of tanh(sqrt(a)) / sqrt(a), as tanhXdX */
#define PADE_A1             105.0f
#define PADE_A0             945.0f
#define PADE_B2             15.0f
#define PADE_B1             420.0f
#define PADE_B0             945.0f

/* Per-lane constants for one block */
struct lanes {
    float32x4_t f, two_f, two_r, f1, inv_f1, f_g0, g0, drive;
    float32x4_t cx, cy0, cy1;       /* linear output: cx*x + cy0*y0 + cy1*y1 */
    uint32x4_t lowpass;             /* lanes whose output is tanh(y1) */
    float32x4_t s1, s2;
};

/* x * tanhXdX(x / 2): the filter's soft clipper */
static inline float32x4_t soft_clip(float32x4_t x)
{
    float32x4_t a = vmulq_n_f32(vmulq_f32(x, x), 0.25f);
    float32x4_t num = vmlaq_f32(vdupq_n_f32(PADE_A0), vaddq_f32(a, vdupq_n_f32(PADE_A1)), a);
    float32x4_t den = vmlaq_f32(vdupq_n_f32(PADE_B0),
                                vmlaq_f32(vdupq_n_f32(PADE_B1), a, vdupq_n_f32(PADE_B2)), a);
    return vmulq_f32(x, vmulq_f32(num, neon_reciprocal(den)));
}

static inline float32x4_t fast_tanh(float32x4_t v)
{
    float32x4_t limit = vdupq_n_f32(TANH_LIMIT);
    float32x4_t r = vmulq_f32(vmulq_n_f32(v, TANH_NUMERATOR),
                              neon_reciprocal(vmlaq_f32(vdupq_n_f32(TANH_DENOMINATOR), v, v)));
    r = vbslq_f32(vcgtq_f32(v, limit), vdupq_n_f32(1.0f), r);
    return vbslq_f32(vcltq_f32(v, vnegq_f32(limit)), vdupq_n_f32(-1.0f), r);
}

static inline float32x4_t zero_nan(float32x4_t v)
{
    return vbslq_f32(vceqq_f32(v, v), v, vdupq_n_f32(0.0f));
}

static int vectorized(uint8_t type)
{
    return type >= FILTER_LP && type <= FILTER_PEAK;
}

static void load(struct lanes *l, ResonantFilter *const *filters, const uint8_t *types)
{
    float f[LANES], two_f[LANES], two_r[LANES], f1[LANES], inv_f1[LANES], f_g0[LANES],
          g0[LANES], drive[LANES], cx[LANES], cy0[LANES], cy1[LANES], s1[LANES], s2[LANES];
    uint32_t lowpass[LANES];
    for (unsigned k = 0; k < LANES; ++k) {
        const ResonantFilter *flt = filters[k];
        if (!flt) {
            f[k] = two_f[k] = two_r[k] = f1[k] = f_g0[k] = drive[k] = cx[k] = cy0[k] = cy1[k] = 0.0f;
            g0[k] = inv_f1[k] = 1.0f; s1[k] = s2[k] = 0.0f; lowpass[k] = 0;
            continue;
        }
        float g = flt->g, r = flt->f >= STABLE_F_LIMIT ? 1.0f : flt->q;
        f[k] = g; two_f[k] = 2.0f * g; two_r[k] = 2.0f * r;
        g0[k] = 1.0f / (1.0f + g * 2.0f * r);
        f1[k] = g * g * g0[k];
        inv_f1[k] = 1.0f / (f1[k] + 1.0f);
        f_g0[k] = g * g0[k];
        drive[k] = flt->drive * INPUT_SCALE;
        s1[k] = flt->s1; s2[k] = flt->s2;
        lowpass[k] = types[k] == FILTER_LP ? ~0u : 0;
        cx[k] = cy0[k] = cy1[k] = 0.0f;
        switch (types[k]) {
        case FILTER_HP: cx[k] = 1.0f; cy0[k] = -two_r[k]; cy1[k] = -1.0f; break;
        case FILTER_BP: cy0[k] = 1.0f; break;
        case FILTER_UNITY_BP: cy0[k] = two_r[k]; break;
        case FILTER_NOTCH: cx[k] = 1.0f; cy0[k] = -two_r[k]; break;
        case FILTER_PEAK: cx[k] = -1.0f; cy0[k] = two_r[k]; cy1[k] = 2.0f; break;
        default: break;
        }
    }
    l->f = vld1q_f32(f); l->two_f = vld1q_f32(two_f); l->two_r = vld1q_f32(two_r);
    l->f1 = vld1q_f32(f1); l->inv_f1 = vld1q_f32(inv_f1); l->f_g0 = vld1q_f32(f_g0);
    l->g0 = vld1q_f32(g0); l->drive = vld1q_f32(drive);
    l->cx = vld1q_f32(cx); l->cy0 = vld1q_f32(cy0); l->cy1 = vld1q_f32(cy1);
    l->lowpass = vld1q_u32(lowpass);
    l->s1 = zero_nan(vld1q_f32(s1)); l->s2 = zero_nan(vld1q_f32(s2));
}

static void store(const struct lanes *l, ResonantFilter *const *filters)
{
    float s1[LANES], s2[LANES];
    vst1q_f32(s1, l->s1);
    vst1q_f32(s2, l->s2);
    for (unsigned k = 0; k < LANES; ++k)
        if (filters[k]) { filters[k]->s1 = s1[k]; filters[k]->s2 = s2[k]; }
}

/* One sample of one group: in holds four voices' input samples. */
static inline int16x4_t step(struct lanes *l, int16x4_t in)
{
    float32x4_t xin = vmulq_f32(vcvtq_f32_s32(vmovl_s16(in)), l->drive);
    float32x4_t x = soft_clip(xin);
    float32x4_t s1 = l->s1, s2 = l->s2;
    float32x4_t y1 = vmulq_f32(vmlaq_f32(vmlaq_f32(s2, l->f1, x), l->f_g0, s1), l->inv_f1);
    float32x4_t xx = vsubq_f32(x, y1);
    float32x4_t clipped = soft_clip(s1);
    float32x4_t y0 = vmulq_f32(vmlaq_f32(clipped, l->f, xx), l->g0);
    l->s1 = vmlaq_f32(clipped, l->two_f, vmlsq_f32(xx, l->two_r, y0));
    l->s2 = vmlaq_f32(s2, l->two_f, y0);
    float32x4_t linear = vmlaq_f32(vmlaq_f32(vmulq_f32(l->cx, x), l->cy0, y0), l->cy1, y1);
    float32x4_t out = vbslq_f32(l->lowpass, fast_tanh(y1), linear);
    return vqmovn_s32(vcvtq_s32_f32(vmulq_n_f32(out, OUTPUT_GAIN)));
}

void filter_bank(ResonantFilter *const *filters, const uint8_t *types, int16_t *const *buffers,
                 unsigned count)
{
    ResonantFilter *lane_filter[BANK_VOICES] = { 0 };
    uint8_t lane_type[BANK_VOICES] = { 0 };
    int16_t *lane_buffer[BANK_VOICES] = { 0 };
    static const int16_t silence[AUDIO_BLOCK];
    unsigned used = 0;
    for (unsigned v = 0; v < count; ++v) {
        if (vectorized(types[v]) && used < BANK_VOICES) {
            lane_filter[used] = filters[v]; lane_type[used] = types[v]; lane_buffer[used] = buffers[v];
            used++;
        } else {
            SVF_calcBlockZDF(filters[v], types[v], buffers[v], AUDIO_BLOCK);
        }
    }
    if (!used) return;

    /* Interleave: in[group][sample] holds four voices' samples */
    int16_t in[GROUPS][AUDIO_BLOCK][LANES], out[GROUPS][AUDIO_BLOCK][LANES];
    for (unsigned g = 0; g < GROUPS; ++g)
        for (unsigned i = 0; i < AUDIO_BLOCK; i += LANES) {
            int16x4x4_t v;
            for (unsigned k = 0; k < LANES; ++k) {
                const int16_t *b = lane_buffer[g * LANES + k];
                v.val[k] = vld1_s16((b ? b : silence) + i);
            }
            vst4_s16(&in[g][i][0], v);
        }

    struct lanes a, b;
    load(&a, &lane_filter[0], &lane_type[0]);
    load(&b, &lane_filter[LANES], &lane_type[LANES]);
    for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
        vst1_s16(out[0][i], step(&a, vld1_s16(in[0][i])));
        vst1_s16(out[1][i], step(&b, vld1_s16(in[1][i])));
    }
    store(&a, &lane_filter[0]);
    store(&b, &lane_filter[LANES]);

    for (unsigned g = 0; g < GROUPS; ++g)
        for (unsigned i = 0; i < AUDIO_BLOCK; i += LANES) {
            int16x4x4_t v = vld4_s16(&out[g][i][0]);
            for (unsigned k = 0; k < LANES; ++k) {
                int16_t *buf = lane_buffer[g * LANES + k];
                if (buf) vst1_s16(buf + i, v.val[k]);
            }
        }
}
