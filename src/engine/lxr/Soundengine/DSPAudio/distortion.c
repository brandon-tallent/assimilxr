/*
 * distortion.c
 *
 *  Created on: 14.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#include "Soundengine/DSPAudio/distortion.h"
#include "Soundengine/config.h"
#include "math.h"
#include "Soundengine/DSPAudio/1PoleLp.h"
#include "lxr_neon.h" /* AssimiLXR */

/* AssimiLXR: (1+s)x/(1+s|x|) on eight samples, shape per sample */
static inline void distort8(int16_t* buf, float32x4_t s_lo, float32x4_t s_hi)
{
	const float32x4_t one = vdupq_n_f32(1.f);
	float32x4_t lo, hi;
	neon_load(buf, &lo, &hi);
	lo = vmulq_n_f32(lo, 1/32767.f);
	hi = vmulq_n_f32(hi, 1/32767.f);
	lo = vmulq_f32(vmulq_f32(vaddq_f32(one, s_lo), lo), neon_reciprocal(vmlaq_f32(one, s_lo, vabsq_f32(lo))));
	hi = vmulq_f32(vmulq_f32(vaddq_f32(one, s_hi), hi), neon_reciprocal(vmlaq_f32(one, s_hi, vabsq_f32(hi))));
	neon_store(buf, vmulq_n_f32(lo, 32767.f), vmulq_n_f32(hi, 32767.f));
}
//--------------------------------------------------
IN_ITCM_RAM __inline void setDistortionShape(Distortion *dist, float shape)
{
	shape -= 0.0078125; //to prevent division by 0
	dist->shape = 2*(shape)/(1-(shape));
}
//--------------------------------------------------
IN_ITCM_RAM void calcDistBlock(const Distortion *dist, int16_t* buf, const uint8_t size)
{
	/* AssimiLXR: NEON */
	const float32x4_t s = vdupq_n_f32(dist->shape);
	for(uint8_t i=0;i<size;i+=8)
		distort8(buf+i, s, s);
}
//--------------------------------------------------
void calcDistBlockFiltered(Distortion *dist, int16_t* buf, const uint8_t size)
{
	/* AssimiLXR: the smoothed shape for the block, then NEON */
	float shape[OUTPUT_DMA_SIZE];
	calcOnePoleBlockFixedInput(&dist->DriveParamFilter, dist->shape, shape, size);
	for(uint8_t i=0;i<size;i+=8)
		distort8(buf+i, vld1q_f32(shape+i), vld1q_f32(shape+i+4));
}
//--------------------------------------------------

float distortion_calcSampleFloat(const Distortion *dist, float x)
{
	return (1+dist->shape)*x/(1+dist->shape*fabsf(x));
}
