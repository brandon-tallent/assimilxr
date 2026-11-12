/* NEON forms of the LXR's per-sample buffer loops. Each matches the scalar
 * code it replaces, including the float-to-int16 conversion: truncate to
 * 32 bits, then keep the low 16. */
#ifndef LXR_NEON_H
#define LXR_NEON_H

#include <arm_neon.h>
#include <stdint.h>

/* 1/x to full float precision: estimate and two Newton steps */
static inline float32x4_t neon_reciprocal(float32x4_t x)
{
    float32x4_t e = vrecpeq_f32(x);
    e = vmulq_f32(e, vrecpsq_f32(x, e));
    return vmulq_f32(e, vrecpsq_f32(x, e));
}

static inline void neon_load(const int16_t *p, float32x4_t *lo, float32x4_t *hi)
{
    int16x8_t v = vld1q_s16(p);
    *lo = vcvtq_f32_s32(vmovl_s16(vget_low_s16(v)));
    *hi = vcvtq_f32_s32(vmovl_s16(vget_high_s16(v)));
}

static inline void neon_store(int16_t *p, float32x4_t lo, float32x4_t hi)
{
    vst1q_s16(p, vcombine_s16(vmovn_s32(vcvtq_s32_f32(lo)), vmovn_s32(vcvtq_s32_f32(hi))));
}

/* buf[i] = buf[i] * gain[i]; size is a multiple of 8 */
static inline void neon_gain_ramp(int16_t *buf, const float *gain, unsigned size)
{
    for (unsigned i = 0; i < size; i += 8) {
        float32x4_t lo, hi;
        neon_load(buf + i, &lo, &hi);
        neon_store(buf + i, vmulq_f32(lo, vld1q_f32(gain + i)), vmulq_f32(hi, vld1q_f32(gain + i + 4)));
    }
}

/* buf[i] = buf[i] * gain */
static inline void neon_gain(int16_t *buf, float gain, unsigned size)
{
    for (unsigned i = 0; i < size; i += 8) {
        float32x4_t lo, hi;
        neon_load(buf + i, &lo, &hi);
        neon_store(buf + i, vmulq_n_f32(lo, gain), vmulq_n_f32(hi, gain));
    }
}

/* buf[i] = saturate(buf[i] + add[i]) */
static inline void neon_add_saturating(int16_t *buf, const int16_t *add, unsigned size)
{
    for (unsigned i = 0; i < size; i += 8)
        vst1q_s16(buf + i, vqaddq_s16(vld1q_s16(buf + i), vld1q_s16(add + i)));
}

/* fm[i] = (uint32_t)(mod[i] * amount); negative products give 0, as the
 * scalar VFP conversion does */
static inline void neon_fm_offsets(const int16_t *mod, float amount, uint32_t *fm, unsigned size)
{
    for (unsigned i = 0; i < size; i += 8) {
        float32x4_t lo, hi;
        neon_load(mod + i, &lo, &hi);
        vst1q_u32(fm + i, vcvtq_u32_f32(vmulq_n_f32(lo, amount)));
        vst1q_u32(fm + i + 4, vcvtq_u32_f32(vmulq_n_f32(hi, amount)));
    }
}

#endif
