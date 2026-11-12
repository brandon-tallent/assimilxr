/*
 * InitKit.h
 *
 *  Created on: Apr 12, 2021
 *      Author: julian
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */

#ifndef INC_PRESET_INITKIT_H_
#define INC_PRESET_INITKIT_H_

#include "lxr_port.h" /* AssimiLXR: replaces the STM32 HAL */
//init kit from hrtlkits_batch3.zip
#define INIT_KIT_SIZE 254
extern const uint8_t initKit[INIT_KIT_SIZE]; /* AssimiLXR: storage class first */
#endif /* INC_PRESET_INITKIT_H_ */
