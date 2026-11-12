/*
 * rnd.c
 *
 *  Created on: 07.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */


#include "Soundengine/DSPAudio/random.h"
#include "lxr_port.h" /* AssimiLXR: replaces the STM32 HAL */
//-------------------------------------------------------------
void initRng()
{
	/* Enable RNG clock source */
	//RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_RNG, ENABLE);

	/* RNG Peripheral enable */
	//RNG_Cmd(ENABLE);
}
//-------------------------------------------------------------
/* AssimiLXR: xorshift32 in place of the STM32 hardware generator */
#define RNG_SEED 0x2545f491u /* any nonzero start */
static uint32_t rngState = RNG_SEED;
uint32_t GetRngValue()
{
	uint32_t x = rngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rngState = x;
	return x;
}
//-------------------------------------------------------------

