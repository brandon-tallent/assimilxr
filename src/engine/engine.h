/* The LXR-02 drum engine on the Assimil8or: each of the eight channels
 * plays an LXR voice on its own output; their stereo mix with the FX bus
 * is on the mix outputs. */
#ifndef ENGINE_H
#define ENGINE_H

#include "audio.h"

#define ENGINE_VOICES       6u      /* Drum 1-3, snare, cymbal, hi-hat */
#define ENGINE_TRIGGERS     7u      /* the voices, with the open hi-hat on its own */
#define ENGINE_CHANNELS     8u
#define ENGINE_NO_VOICE     0xffu
#define ENGINE_STAGES       5u

/* Cycle counts of the last block and the most seen since a reset, by
 * stage: block-rate updates, voices up to their filters, the filters, the
 * voices after them with routing, the FX bus and output. */
struct engine_profile {
    uint32_t last[ENGINE_STAGES];
    uint32_t max[ENGINE_STAGES];
    uint32_t midi_drops;
};
extern volatile struct engine_profile engine_profile;

/* Loads the LXR init kit. Call before audio starts. */
void engine_init(void);

/* Queues a MIDI channel message or realtime byte that arrived at cycles
 * (cycles_now). MIDI receive interrupt only. */
void engine_midi(uint8_t status, uint8_t data1, uint8_t data2, uint32_t cycles);

/* The audio render callback; block_start is the cycle count when the block
 * began. Notes that arrived during the last block play at their own frame
 * of this one, one block after they arrived. */
void engine_render(const struct audio_input *in, struct audio_output *out, uint32_t block_start);

void engine_profile_reset(void);

/* Sets an LXR sound parameter (ParamEnums) of a channel for the next
 * block. FX bus parameters are shared. Any context. */
void engine_parameter(unsigned channel, uint16_t parameter, uint8_t value);
uint8_t engine_parameter_value(unsigned channel, uint16_t parameter);

/* The voice a channel plays: 0-2 Drum 1-3, 3 snare, 4 cymbal, 5 hi-hat,
 * 6 open hi-hat, ENGINE_NO_VOICE none. A voice is also its page set in the
 * LXR menu tables. engine_voice returns -1 for none. */
void engine_set_voice(unsigned channel, unsigned voice);
int engine_voice(unsigned channel);

/* Channel mixing outside the LXR's own voice settings: a mute, and a send
 * of the channel to the FX bus beside its own routing. Any context. */
/* GATED: the channel's play mode, 0 1 Shot (the drum plays out), 1 Gated
 * (it fades out when its gate or note ends) */
enum engine_mix_field { ENGINE_MIX_MUTE, ENGINE_MIX_SEND, ENGINE_MIX_GATED };
#define ENGINE_SEND_MAX     127u
void engine_mix(unsigned channel, enum engine_mix_field field, uint8_t value);
uint8_t engine_mix_value(unsigned channel, enum engine_mix_field field);

/* Replaces a channel's sound parameters (count of them; the rest 0), its
 * voice (ENGINE_KEEP_VOICE to keep it), mute and FX send. Loads are queued
 * and two channels go a block, in order. Main loop only; waits while the
 * queue is full and returns 0 if it stays full. */
#define ENGINE_KEEP_VOICE   0xfeu
int engine_load_channel(unsigned channel, const uint8_t *parameters, uint32_t count,
                        unsigned voice, uint8_t mute, uint8_t send);
int engine_load_done(void);
/* Morph, as the LXR-02's: each channel can hold a morph kit, and every
 * parameter except routing and modulation targets is the blend of the
 * channel's kit and its morph kit by the morph amount (0-255, 0 the kit).
 * engine_load_morph with count 0 clears a channel's. NRPN 120 sets the
 * amount; a CV route on ENGINE_PARAMETER_MORPH moves it. Main loop only. */
#define ENGINE_PARAMETER_MORPH  248u    /* the LXR's PAR_MORPH */
#define ENGINE_MORPH_MAX        255u
int engine_load_morph(unsigned channel, const uint8_t *parameters, uint32_t count);
const uint8_t *engine_channel_morph(unsigned channel);     /* 0 without one */
void engine_channel_blend(unsigned channel, uint8_t *parameters);
void engine_morph(uint8_t amount);
uint8_t engine_morph_value(void);   /* as set */
uint8_t engine_morph_amount(void);  /* with CV */

/* A channel's sound parameters, END_OF_SOUND_PARAMETERS of them */
const uint8_t *engine_channel_parameters(unsigned channel);

/* MIDI notes, as the LXR-02 takes them: each of its seven voices (Drum
 * 1-3, snare, cymbal, hi-hat, open hi-hat) on a channel, at one note (at
 * the kit's tuning) or any note (0: at that note's pitch), playing every
 * channel set to that voice. Notes on the global channel play the selected
 * channel. Besides, the Assimil8or's Chroma 8 assignment (blocks of eight
 * keys from note 36 pick channels 1-8) and General MIDI drum notes, each
 * on its own channel. Control changes are taken on any of these channels.
 * Channels are 0-15. */
#define ENGINE_MIDI_OFF     0xffu
struct engine_midi_setup {
    uint8_t voice_channel[ENGINE_TRIGGERS], voice_note[ENGINE_TRIGGERS];
    uint8_t global_channel, chroma_channel, drum_channel;
    uint8_t selected;       /* the channel the global channel plays; see engine_select */
    uint8_t velocity_always;    /* velocity sets every voice's level, whatever the kit's Velocity level */
};
void engine_midi_setup(const struct engine_midi_setup *setup);
void engine_midi_setup_get(struct engine_midi_setup *setup);
void engine_select(unsigned channel);

/* The Sample inputs, in stereo: send (0-127) to the FX bus and dry
 * (0-127) to the mix outputs. Any context. */
void engine_inputs_audio(uint8_t send, uint8_t dry);

/* A channel's pitch input transposes each of its notes by 12 semitones a
 * volt (1V/oct); its accent input sets the velocity of its gate triggers
 * (0-5 V). Inputs are inputs_cv indexes, ENGINE_NO_INPUT for none. */
#define ENGINE_NO_INPUT     0xffu
void engine_channel_inputs(unsigned channel, uint8_t pitch_input, uint8_t accent_input);
void engine_channel_inputs_get(unsigned channel, uint8_t *pitch_input, uint8_t *accent_input);

/* The tempo: from MIDI clock when it runs, else the default 120 BPM */
float engine_tempo(void);
int engine_clocked(void);

/* Plays a channel at its set pitch. */
void engine_audition(unsigned channel, uint8_t velocity);
/* Ends a channel's note, as its gate falling: a Gated channel fades out */
void engine_release(unsigned channel);

/* A CV route: CV input `input` (inputs_cv index) moves a channel's
 * parameter by `amount` percent of its low-high range at 5 V. */
struct engine_cv_route {
    uint16_t parameter;
    uint8_t channel, input, low, high;
    int8_t amount;
};
#define ENGINE_CV_ROUTES    64u

/* Replaces the CV routes. Main loop only. Returns 0 while the last set is
 * not yet taken by the audio interrupt; call again later. */
int engine_cv_routes(const struct engine_cv_route *routes, unsigned count);

/* Applies the CV routes to the readings in volts and plays the channels
 * whose bits are set in triggers. A trigger whose bit is set in timed
 * plays at its time, cycles[channel]; the others at the block's start.
 * Audio interrupt only, before engine_render. */
void engine_inputs(const float *cv, uint8_t triggers, uint8_t timed, const uint32_t *cycles,
                   uint8_t releases);

/* 1 while a note, the gate or the button holds the channel open */
int engine_held(unsigned channel);

/* 1 while the channel's voice is still audible */
int engine_sounding(unsigned channel);

#endif
