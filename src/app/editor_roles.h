/* The roles of a channel's sound for remote editing. Remote editors build
 * their parameter maps from this table. */
#ifndef APP_EDITOR_ROLES_H
#define APP_EDITOR_ROLES_H

#include <stdint.h>
#include "Parameters.h"

/* A name and section (as the voice pages) and the parameter for each
 * voice: Drum 1-3, snare, cymbal, hi-hat, open hat. PAR_NONE where the
 * voice has none. */
#define EDITOR_VOICES 7u     /* Drum 1-3, snare, cymbal, hi-hat, open hat */
struct editor_role { const char *name; uint8_t section; uint8_t parameter[EDITOR_VOICES]; };
enum { EDITOR_OSC, EDITOR_AMP, EDITOR_MOD, EDITOR_FM, EDITOR_TRANSIENT, EDITOR_FILTER, EDITOR_LFO,
       EDITOR_MIX, EDITOR_SECTIONS };
#define ALL6(p) { p##1, p##2, p##3, p##4, p##5, p##6, p##6 }
#define DRUMS(p) { p##1, p##2, p##3, PAR_NONE, PAR_NONE, PAR_NONE, PAR_NONE }
#define DRUMS_SNARE(p) { p##1, p##2, p##3, p##4, PAR_NONE, PAR_NONE, PAR_NONE }
#define METAL(cym, hat) { PAR_NONE, PAR_NONE, PAR_NONE, PAR_NONE, cym, hat, hat }
static const struct editor_role editor_roles[] = {
    { "Coarse",         EDITOR_OSC, ALL6(PAR_COARSE) },
    { "Fine",           EDITOR_OSC, ALL6(PAR_FINE) },
    { "Waveform",       EDITOR_OSC, { PAR_OSC_WAVE_DRUM1, PAR_OSC_WAVE_DRUM2, PAR_OSC_WAVE_DRUM3, PAR_OSC_WAVE_SNARE,
                                       PAR_WAVE1_CYM, PAR_WAVE1_HH, PAR_WAVE1_HH } },
    { "Pulse width",    EDITOR_OSC, ALL6(PAR_PWM_) },
    { "Noise freq",     EDITOR_OSC, { PAR_NONE, PAR_NONE, PAR_NONE, PAR_NOISE_FREQ1, PAR_NONE, PAR_NONE, PAR_NONE } },
    { "Noise mix",      EDITOR_OSC, { PAR_NONE, PAR_NONE, PAR_NONE, PAR_MIX1, PAR_NONE, PAR_NONE, PAR_NONE } },
    { "Attack",         EDITOR_AMP, ALL6(PAR_VELOA) },
    { "Decay",          EDITOR_AMP, { PAR_VELOD1, PAR_VELOD2, PAR_VELOD3, PAR_VELOD4, PAR_VELOD5, PAR_VELOD6_CLOSED,
                                       PAR_VELOD6_OPEN } },
    { "Slope",          EDITOR_AMP, ALL6(PAR_VOL_SLOPE) },
    { "Repeats",        EDITOR_AMP, { PAR_NONE, PAR_NONE, PAR_NONE, PAR_REPEAT4, PAR_REPEAT5, PAR_NONE, PAR_NONE } },
    { "Pitch decay",    EDITOR_MOD, DRUMS_SNARE(PAR_MOD_EG) },
    { "Pitch slope",    EDITOR_MOD, DRUMS_SNARE(PAR_PITCH_SLOPE) },
    { "Pitch amount",   EDITOR_MOD, DRUMS_SNARE(PAR_MODAMNT) },
    { "Velocity to",    EDITOR_MOD, ALL6(PAR_VEL_DEST_) },
    { "Velocity amt",   EDITOR_MOD, ALL6(PAR_VELO_MOD_AMT_) },
    { "Velocity level", EDITOR_MOD, ALL6(PAR_VOLUME_MOD_ON_OFF) },
    { "FM amount",      EDITOR_FM, DRUMS(PAR_FMAMNT) },
    { "FM freq",        EDITOR_FM, DRUMS(PAR_FM_FREQ) },
    { "FM wave",        EDITOR_FM, { PAR_MOD_WAVE_DRUM1, PAR_MOD_WAVE_DRUM2, PAR_MOD_WAVE_DRUM3, PAR_NONE,
                                      PAR_WAVE2_CYM, PAR_WAVE2_HH, PAR_WAVE2_HH } },
    { "FM mode",        EDITOR_FM, DRUMS(PAR_MIX_MOD_) },
    { "Mod 1 freq",     EDITOR_FM, METAL(PAR_MOD_OSC_F1_CYM, PAR_MOD_OSC_F1) },
    { "Mod 2 freq",     EDITOR_FM, METAL(PAR_MOD_OSC_F2_CYM, PAR_MOD_OSC_F2) },
    { "Mod 1 level",    EDITOR_FM, METAL(PAR_MOD_OSC_GAIN1_CYM, PAR_MOD_OSC_GAIN1) },
    { "Mod 2 level",    EDITOR_FM, METAL(PAR_MOD_OSC_GAIN2_CYM, PAR_MOD_OSC_GAIN2) },
    { "Mod 2 wave",     EDITOR_FM, METAL(PAR_WAVE3_CYM, PAR_WAVE3_HH) },
    { "Transient wave", EDITOR_TRANSIENT, { PAR_TRANS1_WAVE, PAR_TRANS2_WAVE, PAR_TRANS3_WAVE, PAR_TRANS4_WAVE,
                                            PAR_TRANS5_WAVE, PAR_TRANS6_WAVE, PAR_TRANS6_WAVE } },
    { "Transient level", EDITOR_TRANSIENT, { PAR_TRANS1_VOL, PAR_TRANS2_VOL, PAR_TRANS3_VOL, PAR_TRANS4_VOL,
                                              PAR_TRANS5_VOL, PAR_TRANS6_VOL, PAR_TRANS6_VOL } },
    { "Transient freq", EDITOR_TRANSIENT, { PAR_TRANS1_FREQ, PAR_TRANS2_FREQ, PAR_TRANS3_FREQ, PAR_TRANS4_FREQ,
                                             PAR_TRANS5_FREQ, PAR_TRANS6_FREQ, PAR_TRANS6_FREQ } },
    { "Cutoff",         EDITOR_FILTER, ALL6(PAR_FILTER_FREQ_) },
    { "Resonance",      EDITOR_FILTER, ALL6(PAR_RESO_) },
    { "Filter type",    EDITOR_FILTER, ALL6(PAR_FILTER_TYPE_) },
    { "Filter drive",   EDITOR_FILTER, ALL6(PAR_FILTER_DRIVE_) },
    { "LFO rate",       EDITOR_LFO, ALL6(PAR_FREQ_LFO) },
    { "LFO sync",       EDITOR_LFO, ALL6(PAR_SYNC_LFO) },
    { "LFO amount",     EDITOR_LFO, ALL6(PAR_AMOUNT_LFO) },
    { "LFO wave",       EDITOR_LFO, ALL6(PAR_WAVE_LFO) },
    { "LFO retrigger",  EDITOR_LFO, ALL6(PAR_RETRIGGER_LFO) },
    { "LFO phase",      EDITOR_LFO, ALL6(PAR_OFFSET_LFO) },
    { "LFO voice",      EDITOR_LFO, ALL6(PAR_VOICE_LFO) },
    { "LFO target",     EDITOR_LFO, ALL6(PAR_TARGET_LFO) },
    { "Level",          EDITOR_MIX, ALL6(PAR_VOL) },
    { "Pan",            EDITOR_MIX, ALL6(PAR_PAN) },
    { "Sample rate",    EDITOR_MIX, ALL6(PAR_VOICE_DECIMATION) },
    { "Drive",          EDITOR_MIX, { PAR_DRIVE1, PAR_DRIVE2, PAR_DRIVE3, PAR_SNARE_DISTORTION, PAR_CYMBAL_DISTORTION,
                                       PAR_HAT_DISTORTION, PAR_HAT_DISTORTION } },
    { "Output",         EDITOR_MIX, ALL6(PAR_AUDIO_OUT) },
};
#define EDITOR_ROLES (sizeof editor_roles / sizeof editor_roles[0])
#endif
