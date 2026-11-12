/*
 * Snare.c
 *
 *  Created on: 17.04.2012
 *
 * Copyright (C) 2012-2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of the LXR firmware. It is licensed under the GNU General
 * Public License version 3, with the additional permission and terms in
 * NOTICE.md. See LICENSE and NOTICE.md in the root of the repository.
 */



#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/squareRootLut.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "lxr_shell.h" /* AssimiLXR: was Soundengine/TriggerOut.h */
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "lxr_shell.h" /* AssimiLXR: was IO/adcPots.h */
#include "Soundengine/FxBus.h"
#include "lxr_neon.h" /* AssimiLXR */
//instance of the snare voice
SnareVoice *lxr_snare; /* AssimiLXR: the selected channel's voice */

//---------------------------------------------------
void Snare_setPan(const uint8_t pan)
{
	//snareVoice.panL = squareRootLut[127-pan];
	//snareVoice.panR = squareRootLut[pan];
	snareVoice.pan = pan;
}
//---------------------------------------------------
void Snare_init()
{
	initOnePole(&snareVoice.FaderVolumeFilter);
	setOnePoleCoef(&snareVoice.FaderVolumeFilter,0.1);

	SnapEg_init(&snareVoice.snapEg);
	Snare_setPan(0);
	snareVoice.vol = 0.8f;

	//snareVoice.panModifier = 1.f;

	snareVoice.noiseOsc.freq = 440;
	snareVoice.noiseOsc.waveform = 1;
	snareVoice.noiseOsc.fmMod = 0;
	snareVoice.noiseOsc.midiFreq = 70<<8 | 64;
	snareVoice.noiseOsc.pitchMod = 1.0f;
	snareVoice.noiseOsc.modNodeValue = 1;

	snareVoice.osc.freq = 440;
	snareVoice.osc.waveform = 1;
	snareVoice.osc.fmMod = 0;
	snareVoice.osc.midiFreq = 70<<8 | 64;
	snareVoice.osc.modNodeValue = 1;

	setDistortionShape(&snareVoice.distortion, 0.f);

	snareVoice.volumeMod = 1;

	transient_init(&snareVoice.transGen);

	DecayEg_init(&snareVoice.oscPitchEg);
	snareVoice.egPitchModAmount = 0.5f;

	slopeEg2_init(&snareVoice.oscVolEg);



	SVF_init(&snareVoice.filter);

	lfo_init(&snareVoice.lfo);
}
//---------------------------------------------------
void Snare_updatePan()
{
	snareVoice.triggeredPanL = sqrtf((127-snareVoice.pan)/127.f);
	snareVoice.triggeredPanR = sqrtf(snareVoice.pan/127.f);
}
//---------------------------------------------------
void Snare_trigger(const uint8_t vel, const uint8_t note)
{
	lfo_retrigger(3);
	//update velocity modulation
	modNode_updateValue(&velocityModulators[3],vel/127.f);

	float offset = 1;
	if(snareVoice.transGen.waveform==1) //offset mode
	{
		offset -= snareVoice.transGen.volume;
	}
	if(snareVoice.osc.waveform == SINE)
		snareVoice.osc.phase = (0x3ff<<20)*offset;//voiceArray[voiceNr].osc.startPhase ;
	else if(snareVoice.osc.waveform > SINE && snareVoice.osc.waveform <= REC)
		snareVoice.osc.phase = (0xff<<22)*offset;
	else
		snareVoice.osc.phase = 0;

	DecayEg_trigger(&snareVoice.oscPitchEg);
	slopeEg2_trigger(&snareVoice.oscVolEg);
	snareVoice.velo = vel/127.f;

	osc_setBaseNote(&snareVoice.osc,note);
	//TODO noise muss mit transponiert werden

	transient_trigger(&snareVoice.transGen);

	SnapEg_trigger(&snareVoice.snapEg);

	Snare_updatePan();
}
//---------------------------------------------------
IN_ITCM_RAM void Snare_calcAsync()
{
	//add modulation eg to osc freq (1 = no change. a+eg = original freq + modulation
	const float egPitchVal = DecayEg_calc(&snareVoice.oscPitchEg);
	const float pitchEgValue = egPitchVal*snareVoice.egPitchModAmount;
	snareVoice.osc.pitchMod = 1+pitchEgValue;

	//calc the osc  vol eg
	snareVoice.egValueOscVol = slopeEg2_calc(&snareVoice.oscVolEg);

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/*
	if(trigger_isGateModeOn())
	{
		if(!snareVoice.egValueOscVol) {
			//trigger_triggerVoice(TRIGGER_4, TRIGGER_OFF);
			//voiceControl_noteOff(TRIGGER_4);
		}
	}
	*/

	//calc snap EG if transient sample 0 is activated
	if(snareVoice.transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&snareVoice.snapEg, snareVoice.transGen.pitch);
		snareVoice.osc.pitchMod += snapVal*snareVoice.transGen.volume;;
	}

	osc_setFreq(&snareVoice.osc);
	osc_setFreq(&snareVoice.noiseOsc);


}
//---------------------------------------------------
/* AssimiLXR: split at the filter, which the engine runs for all voices at
 * once; the mix and volume are a NEON pass. */
void Snare_calcSyncBlockPre(int16_t* buf, const uint8_t size)
{
	calcNoiseBlock(&snareVoice.noiseOsc,buf,size,0.9f);
}
//---------------------------------------------------
void Snare_calcSyncBlockPost(int16_t* buf, const uint8_t size)
{
	int16_t transBuf[size];
	int16_t oscBuf[size];
	float fader[OUTPUT_DMA_SIZE];

	//calc next osc sample
	calcNextOscSampleBlock(&snareVoice.osc,oscBuf,size,(1.f-snareVoice.mix), snareVoice.pwm);

	//calc transient sample
	transient_calcBlock(&snareVoice.transGen,transBuf,size);

	if(snareVoice.volumeMod || lxr_velocity_always) /* AssimiLXR: or the global setting */
		snareVoice.combinedVolume = snareVoice.vol * snareVoice.velo * snareVoice.egValueOscVol ;
	else
		snareVoice.combinedVolume = snareVoice.vol * snareVoice.egValueOscVol;

	//noise * mix + osc + transient, times volume and the smoothed fader
	calcOnePoleBlockFixedInput(&snareVoice.FaderVolumeFilter, adc_getFaderVolume(3), fader, size);
	for(uint8_t j=0;j<size;j+=8)
	{
		float32x4_t n_lo, n_hi, o_lo, o_hi, t_lo, t_hi;
		neon_load(buf+j, &n_lo, &n_hi);
		neon_load(oscBuf+j, &o_lo, &o_hi);
		neon_load(transBuf+j, &t_lo, &t_hi);
		float32x4_t lo = vaddq_f32(vmlaq_n_f32(o_lo, n_lo, snareVoice.mix), t_lo);
		float32x4_t hi = vaddq_f32(vmlaq_n_f32(o_hi, n_hi, snareVoice.mix), t_hi);
		lo = vmulq_f32(vmulq_n_f32(lo, snareVoice.combinedVolume), vld1q_f32(fader+j));
		hi = vmulq_f32(vmulq_n_f32(hi, snareVoice.combinedVolume), vld1q_f32(fader+j+4));
		neon_store(buf+j, lo, hi);
	}

	calcDistBlock(&snareVoice.distortion,buf,size);
}
//------------------------------------------------------------------------
