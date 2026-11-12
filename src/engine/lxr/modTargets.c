/*
 * modTargets.c
 *
 * The modulation targets from Cc2Text.c, without their display names: a
 * kit stores each LFO and velocity target as an index into this list.
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 * AssimiLXR: reduced to the parameter column.
 */
#include "modTargets.h"

const uint16_t modTargets[] = {
	// nothing
	PAR_NONE,
	// not applicable to any one voice (will only work for autom target)
	PAR_VOICE_DECIMATION_ALL,	//1
	//////////////////////////////////////////////////
	//Voice 1 - Drum 1
	//  1 Oscillator
	PAR_COARSE1,	//2
	PAR_FINE1,
	PAR_OSC_WAVE_DRUM1,
	PAR_PWM_1,
	//  2 Amp EG
	PAR_VELOA1,
	PAR_VELOD1,
	PAR_VOL_SLOPE1,
	//  3 Modulation
	PAR_MOD_EG1,
	PAR_PITCH_SLOPE1,	//10
	PAR_MODAMNT1,	//11
	PAR_VEL_DEST_1,
	PAR_VELO_MOD_AMT_1,
	PAR_VOLUME_MOD_ON_OFF1,
	//  4 FM
	PAR_FMAMNT1,
	PAR_FM_FREQ1,
	PAR_MOD_WAVE_DRUM1,
	PAR_MIX_MOD_1,
	//  5 transient sample
	PAR_TRANS1_WAVE,
	PAR_TRANS1_VOL,	//20
	PAR_TRANS1_FREQ,	//21
	//  6 filter
	PAR_FILTER_FREQ_1,
	PAR_RESO_1,
	PAR_FILTER_TYPE_1,
	PAR_FILTER_DRIVE_1,
	//  7 LFO
	PAR_FREQ_LFO1,
	PAR_SYNC_LFO1,
	PAR_AMOUNT_LFO1,
	PAR_WAVE_LFO1,
	PAR_RETRIGGER_LFO1,	//30
	PAR_OFFSET_LFO1,	//31
	//  8 MIX
	PAR_VOL1,
	PAR_PAN1,
	PAR_VOICE_DECIMATION1,
	PAR_DRIVE1,
	// FX
	PAR_FX_P1,
	PAR_FX_P2,
	PAR_FX_P3,
	PAR_FX_P4,	//39
	//////////////////////////////////////////////////
	//Voice 2 - drum 2
	//  1 Oscillator
	PAR_COARSE2,	//40
	PAR_FINE2,
	PAR_OSC_WAVE_DRUM2,
	PAR_PWM_2,
	//  2 Amp EG
	PAR_VELOA2,
	PAR_VELOD2,
	PAR_VOL_SLOPE2,
	//  3 Modulation
	PAR_MOD_EG2,
	PAR_PITCH_SLOPE2,
	PAR_MODAMNT2,
	PAR_VEL_DEST_2,	//50
	PAR_VELO_MOD_AMT_2,
	PAR_VOLUME_MOD_ON_OFF2,
	//  4 FM
	PAR_FMAMNT2,
	PAR_FM_FREQ2,
	PAR_MOD_WAVE_DRUM2,
	PAR_MIX_MOD_2,
	//  5 transient sample
	PAR_TRANS2_WAVE,
	PAR_TRANS2_VOL,
	PAR_TRANS2_FREQ,
	//  6 filter
	PAR_FILTER_FREQ_2,	//60
	PAR_RESO_2,
	PAR_FILTER_TYPE_2,
	PAR_FILTER_DRIVE_2,
	//  7 LFO
	PAR_FREQ_LFO2,
	PAR_SYNC_LFO2,
	PAR_AMOUNT_LFO2,
	PAR_WAVE_LFO2,
	PAR_RETRIGGER_LFO2,
	PAR_OFFSET_LFO2,
	//  8 MIX
	PAR_VOL2,	//70
	PAR_PAN2,
	PAR_VOICE_DECIMATION2,
	PAR_DRIVE2,
	// FX
	PAR_FX_P1,
	PAR_FX_P2,
	PAR_FX_P3,
	PAR_FX_P4,	//77
	//////////////////////////////////////////////////
	//Voice 3 - drum 3
	//  1 Oscillator
	PAR_COARSE3,
	PAR_FINE3,
	PAR_OSC_WAVE_DRUM3,	//80
	PAR_PWM_3,
	//  2 Amp EG
	PAR_VELOA3,
	PAR_VELOD3,
	PAR_VOL_SLOPE3,
	//  3 Modulation
	PAR_MOD_EG3,
	PAR_PITCH_SLOPE3,
	PAR_MODAMNT3,
	PAR_VEL_DEST_3,
	PAR_VELO_MOD_AMT_3,
	PAR_VOLUME_MOD_ON_OFF3,	//90
	//  4 FM
	PAR_FMAMNT3,
	PAR_FM_FREQ3,
	PAR_MOD_WAVE_DRUM3,
	PAR_MIX_MOD_3,
	//  5 transient sample
	PAR_TRANS3_WAVE,
	PAR_TRANS3_VOL,
	PAR_TRANS3_FREQ,
	//  6 filter
	PAR_FILTER_FREQ_3,
	PAR_RESO_3,
	PAR_FILTER_TYPE_3,	//100
	PAR_FILTER_DRIVE_3,
	//  7 LFO
	PAR_FREQ_LFO3,
	PAR_SYNC_LFO3,
	PAR_AMOUNT_LFO3,
	PAR_WAVE_LFO3,
	PAR_RETRIGGER_LFO3,
	PAR_OFFSET_LFO3,
	//  8 MIX
	PAR_VOL3,
	PAR_PAN3,
	PAR_VOICE_DECIMATION3,	//110
	PAR_DRIVE3,
	// FX
	PAR_FX_P1,
	PAR_FX_P2,
	PAR_FX_P3,
	PAR_FX_P4,	//115
	//////////////////////////////////////////////////
	//Voice 4 - snare/clap
	//  1 Oscillator
	PAR_COARSE4,
	PAR_FINE4,
	PAR_NOISE_FREQ1,
	PAR_MIX1,
	PAR_OSC_WAVE_SNARE,	//120
	PAR_PWM_4,
	//  2 Amp EG
	PAR_VELOA4,
	PAR_VELOD4,
	PAR_REPEAT4,
	PAR_VOL_SLOPE4,
	//  3 Modulation
	PAR_MOD_EG4,
	PAR_PITCH_SLOPE4,
	PAR_MODAMNT4,
	PAR_VEL_DEST_4,
	PAR_VELO_MOD_AMT_4,	//130
	PAR_VOLUME_MOD_ON_OFF4,
	//  4 FM (no entries)
	//  5 transient sample
	PAR_TRANS4_WAVE,
	PAR_TRANS4_VOL,
	PAR_TRANS4_FREQ,
	//  6 filter
	PAR_FILTER_FREQ_4,
	PAR_RESO_4,
	PAR_FILTER_TYPE_4,
	PAR_FILTER_DRIVE_4,
	//  7 LFO
	PAR_FREQ_LFO4,
	PAR_SYNC_LFO4,	//140
	PAR_AMOUNT_LFO4,
	PAR_WAVE_LFO4,
	PAR_RETRIGGER_LFO4,
	PAR_OFFSET_LFO4,
	//  8 MIX
	PAR_VOL4,
	PAR_PAN4,
	PAR_VOICE_DECIMATION4,
	PAR_SNARE_DISTORTION,
	// FX
	PAR_FX_P1,
	PAR_FX_P2,	//150
	PAR_FX_P3,
	PAR_FX_P4,
	//////////////////////////////////////////////////
	//Voice 5 - cymbal
	//  1 Oscillator
	PAR_COARSE5,
	PAR_FINE5,
	PAR_WAVE1_CYM,
	PAR_PWM_5,
	//  2 Amp EG
	PAR_VELOA5,
	PAR_VELOD5,
	PAR_REPEAT5,
	PAR_VOL_SLOPE5,	//160
	//  3 Modulation
	PAR_VEL_DEST_5,
	PAR_VELO_MOD_AMT_5,
	PAR_VOLUME_MOD_ON_OFF5,
	//  4 FM
	PAR_MOD_OSC_F1_CYM,
	PAR_MOD_OSC_F2_CYM,
	PAR_MOD_OSC_GAIN1_CYM,
	PAR_MOD_OSC_GAIN2_CYM,
	PAR_WAVE2_CYM,
	PAR_WAVE3_CYM,
	//  5 transient sample
	PAR_TRANS5_WAVE,	//170
	PAR_TRANS5_VOL,
	PAR_TRANS5_FREQ,
	//  6 filter
	PAR_FILTER_FREQ_5,
	PAR_RESO_5,
	PAR_FILTER_TYPE_5,
	PAR_FILTER_DRIVE_5,
	//  7 LFO
	PAR_FREQ_LFO5,
	PAR_SYNC_LFO5,
	PAR_AMOUNT_LFO5,
	PAR_WAVE_LFO5,	//180
	PAR_RETRIGGER_LFO5,
	PAR_OFFSET_LFO5,
	//  8 MIX
	PAR_VOL5,
	PAR_PAN5,
	PAR_VOICE_DECIMATION5,
	PAR_CYMBAL_DISTORTION,
	// FX
	PAR_FX_P1,
	PAR_FX_P2,
	PAR_FX_P3,
	PAR_FX_P4,	//190
	//////////////////////////////////////////////////
	//Voice 6 - HH open and closed
	//  1 Oscillator
	PAR_COARSE6,
	PAR_FINE6,
	PAR_WAVE1_HH,
	PAR_PWM_6,
	//  2 Amp EG
	PAR_VELOA6,
	PAR_VELOD6_CLOSED,
	PAR_VELOD6_OPEN,
	PAR_VOL_SLOPE6,
	//  3 Modulation
	PAR_VEL_DEST_6,
	PAR_VELO_MOD_AMT_6,	//200
	PAR_VOLUME_MOD_ON_OFF6,
	//  4 FM
	PAR_MOD_OSC_F1,
	PAR_MOD_OSC_F2,
	PAR_MOD_OSC_GAIN1,
	PAR_MOD_OSC_GAIN2,
	PAR_WAVE2_HH,
	PAR_WAVE3_HH,
	//  5 transient sample
	PAR_TRANS6_WAVE,
	PAR_TRANS6_VOL,
	PAR_TRANS6_FREQ,	//210
	//  6 filter
	PAR_FILTER_FREQ_6,
	PAR_RESO_6,
	PAR_FILTER_TYPE_6,
	PAR_FILTER_DRIVE_6,
	//  7 LFO
	PAR_FREQ_LFO6,
	PAR_SYNC_LFO6,
	PAR_AMOUNT_LFO6,
	PAR_WAVE_LFO6,
	PAR_RETRIGGER_LFO6,
	PAR_OFFSET_LFO6,	//220
	//  8 MIX
	PAR_VOL6,
	PAR_PAN6,
	PAR_VOICE_DECIMATION6,
	PAR_HAT_DISTORTION,
	// FX
	PAR_FX_P1,
	PAR_FX_P2,
	PAR_FX_P3,
	PAR_FX_P4,	//228
};

const uint8_t modTargetCount = sizeof modTargets / sizeof modTargets[0];
