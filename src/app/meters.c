/* Peak levels of every output and input, held and decaying, for the screen. */
#include "meters.h"
#include <arm_neon.h>

#define PEAK_DECAY          0.999f

volatile float meters_peak[METERS];

static void track(unsigned index, const float *x)
{
    float32x4_t peak = vdupq_n_f32(0.0f);
    for (unsigned i = 0; i < AUDIO_BLOCK; i += 4) peak = vmaxq_f32(peak, vabsq_f32(vld1q_f32(x + i)));
    float32x2_t pair = vpmax_f32(vget_low_f32(peak), vget_high_f32(peak));
    float block = vget_lane_f32(vpmax_f32(pair, pair), 0), held = meters_peak[index] * PEAK_DECAY;
    meters_peak[index] = block > held ? block : held;
}

void meters_update(const struct audio_input *in, const struct audio_output *out)
{
    for (unsigned c = 0; c < AUDIO_CHANNELS; ++c) track(c, out->channel[c]);
    track(AUDIO_CHANNELS, out->mix[0]);
    track(AUDIO_CHANNELS + 1, out->mix[1]);
    track(AUDIO_CHANNELS + 2, in->sample[0]);
    track(AUDIO_CHANNELS + 3, in->sample[1]);
}
