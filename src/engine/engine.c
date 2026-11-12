/*
 * The LXR-02 drum engine on the Assimil8or's outputs.
 *
 * Based on mixer.c, Soundengine.c, presetManager.c and MidiParser.c of the
 * LXR firmware, Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Each Assimil8or channel holds a context: a full set of LXR voices with
 * their own parameters, of which the channel plays one. The LXR sources work
 * on the selected context through pointers (voiceArray, snareVoice and the
 * rest). A voice renders a block of 16-bit samples, as on the LXR-02; the
 * block goes to the channel's output and, panned, to the stereo mix or the
 * FX bus by the voice's audio routing.
 */
#include "engine.h"
#include "cpu.h"
#include "modTargets.h"
#include "InitKit.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "Soundengine/MIDI/MidiVoiceControl.h"
#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/FxBus.h"
#include "filter_bank.h"
#include "lxr_neon.h"
#include <string.h>

_Static_assert(OUTPUT_DMA_SIZE == AUDIO_BLOCK, "the engine renders whole audio blocks");

#define SAMPLE_SCALE        (1.0f / 32768.0f)
#define KIT_NAME_BYTES      8u
#define DEFAULT_BPM         120.0f
#define TRIGGER_HAT_CLOSED  5u
#define TRIGGER_HAT_OPEN    6u
enum { STAGE_BLOCK, STAGE_VOICES, STAGE_FILTERS, STAGE_AFTER, STAGE_OUTPUT };

/* MIDI */
#define MIDI_TYPE_MASK      0xf0u
#define MIDI_CHANNEL_MASK   0x0fu
#define CHROMA_FIRST_NOTE   36u     /* blocks of eight keys from here pick channels 1-8 */
#define CHROMA_BLOCK        8u
#define CHROMA_ROOT_KEY     4u      /* the key in a block that plays at the set pitch */
#define MIDI_RING           64u     /* messages; a power of two */
#define MIDI_PER_BLOCK      16u
#define CC_PARAMETERS       128u    /* CC n sets parameter n - 1; NRPN n sets 128 + n */
#define NRPN_NUMBER_BITS    7u
#define MIDI_DATA_MASK      0x7fu
/* System common status bytes, which the MIDI receiver never passes on,
 * mark the UI's own messages in the queue. Realtime bytes (0xf8 up) do
 * arrive here: 0xf8 is MIDI Clock. */
#define MESSAGE_PARAMETER   0xf4u   /* parameter in bits 8-16, channel 17-19, value 24-31 */
#define MESSAGE_VOICE       0xf5u   /* channel in bits 8-15, voice 16-23 */
#define MESSAGE_AUDITION    0xf7u   /* channel in bits 8-15, velocity 16-23 */
#define MESSAGE_MIX         0xf6u   /* channel in bits 8-15, field 16-23, value 24-31 */
#define MESSAGE_RELEASE     0xf3u   /* channel in bits 8-15 */
#define RELEASE_BLOCKS      8u      /* a Gated channel fades out over 5.3 ms at its release */
#define MESSAGE_PARAMETER_SHIFT 8
#define MESSAGE_PARAMETER_MASK 0x1ffu
#define MESSAGE_CHANNEL_SHIFT 17
#define MESSAGE_CHANNEL_MASK 7u
#define MESSAGE_VALUE_SHIFT 24

#define FPSCR_FLUSH_TO_ZERO (1u << 24)

#define GATE_VELOCITY       100u
#define LOAD_WAIT_CYCLES    (CPU_HZ / 10u)  /* a block takes 0.67 ms */
#define CV_FULL_SCALE_VOLTS 5.0f
#define CV_PERCENT          100.0f

/* General MIDI drum notes on channel 10 */
static const struct { uint8_t note, trigger; } drum_map[] = {
    { 35, 0 }, { 36, 0 },           /* bass drums: Drum 1 */
    { 41, 1 }, { 43, 1 }, { 45, 1 },/* low toms: Drum 2 */
    { 47, 2 }, { 48, 2 }, { 50, 2 },/* high toms: Drum 3 */
    { 37, 3 }, { 38, 3 }, { 40, 3 },/* snares and side stick */
    { 49, 4 }, { 51, 4 }, { 57, 4 },/* crash and ride: Cymbal */
    { 42, 5 }, { 44, 5 },           /* closed and pedal hi-hat */
    { 46, TRIGGER_HAT_OPEN },
};

/* The voice each channel plays at start */
static const uint8_t default_voice[ENGINE_CHANNELS] = {
    0,                  /* 1: Drum 1, the kick */
    3,                  /* 2: snare */
    5,                  /* 3: closed hi-hat */
    1,                  /* 4: Drum 2 */
    2,                  /* 5: Drum 3 */
    4,                  /* 6: cymbal */
    TRIGGER_HAT_OPEN,   /* 7: open hi-hat */
    ENGINE_NO_VOICE,    /* 8 */
};

struct context {
    DrumVoice drums[NUM_DRUM_VOICES];
    SnareVoice snare;
    CymbalVoice cymbal;
    HiHatVoice hat;
    ModulationNode velocity[ENGINE_VOICES];
    Parameter parameters[END_OF_SOUND_PARAMETERS];
    uint8_t values[NUM_PARAMS];                 /* in effect: what the LXR code reads */
    uint8_t kit[END_OF_SOUND_PARAMETERS];       /* as set, before morph and CV */
    uint8_t morph[END_OF_SOUND_PARAMETERS];     /* the morph kit's parameters */
    uint16_t applied[END_OF_SOUND_PARAMETERS];  /* the value last sent to the voices */
    uint8_t routed[END_OF_SOUND_PARAMETERS];    /* set by a CV route, not the morph sweep */
    uint8_t has_morph;
    float decimation_rate[ENGINE_VOICES + 1];
    uint8_t routing[ENGINE_VOICES];
};

/* A Gated channel stays open while any note, its gate or its button is held */
#define HOLD_GATE           128u
#define HOLD_BUTTON         129u
#define HOLD_WORDS          5u

struct channel {
    uint8_t voice;          /* 0-2 drums, 3 snare, 4 cymbal, 5 hi-hat, 6 open hi-hat */
    uint8_t mute, send;     /* send: 0-ENGINE_SEND_MAX to the FX bus */
    uint8_t pitch_input, accent_input;  /* CV inputs, ENGINE_NO_INPUT for none */
    uint8_t gated;          /* Gated play mode */
    uint32_t held[HOLD_WORDS];  /* what holds it open: MIDI keys, the gate, the button */
    float gain;             /* 0 muted or released, 1 playing */
    float decimation_count;
    int16_t decimation_hold;
    /* A note waiting for its frame of this block (see engine_render) */
    uint8_t pending, pending_note, pending_velocity, pending_timed;
    uint32_t pending_cycles;
    /* Voice blocks start at frame phase of each output block; carry holds
     * the part of the last one that falls in the next block. */
    uint8_t phase;
    int16_t carry[AUDIO_BLOCK] __attribute__((aligned(16)));
};

static struct context contexts[ENGINE_CHANNELS];
static struct channel channels[ENGINE_CHANNELS];

/* What the engine sources reach through: the selected context's */
uint8_t *activeParameters;
float *mixer_decimation_rate;
uint8_t *mixer_audioRouting;

volatile struct engine_profile engine_profile;
/* A bit per channel whose last block peaked above SOUNDING_PEAK */
static volatile uint8_t sounding;
#define SOUNDING_PEAK       150     /* about -47 dBFS */

/* CV routes in two banks: the main loop fills the idle one and marks it
 * pending; the audio interrupt takes it at the next block. */
static struct engine_cv_route cv_banks[2][ENGINE_CV_ROUTES];
static unsigned cv_counts[2];
static volatile unsigned cv_active, cv_pending;
static int16_t cv_applied[ENGINE_CV_ROUTES];    /* the value last sent, -1 for none */

/* Morph: each parameter is the blend of the kit and the morph kit at
 * morph_amount (0 the kit, 255 the morph kit), which is morph_base moved
 * by any CV route. A change sweeps the parameters, SWEEP_PER_BLOCK a block,
 * sending those whose value changed. */
#define MORPH_MAX           255u
#define SWEEP_PER_BLOCK     96u
#define APPLIED_UNKNOWN     0xffffu
static volatile uint8_t morph_base;
static uint8_t morph_amount;
static unsigned sweep_left, sweep_at;

/* A pass starts at the first parameter of the LXR's load order */
static void sweep_start(void)
{
    sweep_left = ENGINE_CHANNELS * END_OF_SOUND_PARAMETERS;
    sweep_at = 0;
}

/* Channel and morph loads queued for the audio interrupt. A channel load
 * costs up to 90,000 cycles, so LOADS_PER_BLOCK of them go a block: a
 * whole kit lands in 4 blocks (2.7 ms). Morph loads only copy. */
#define LOAD_QUEUE          32u     /* a power of two: two kits of channel and morph loads */
#define LOADS_PER_BLOCK     2u
struct load {
    uint8_t parameters[END_OF_SOUND_PARAMETERS];
    uint32_t count;
    uint8_t channel, voice, mute, send, into_morph;
};
static struct load load_queue[LOAD_QUEUE];
static volatile unsigned load_write, load_read;

static uint8_t muted;
static void take_load(void);

/* MIDI channels are 0-15 here, ENGINE_MIDI_OFF for none */
static struct engine_midi_setup midi_setup = {
    .voice_channel = { 0, 0, 0, 0, 0, 0, 0 },
    .voice_note = { 36, 37, 38, 39, 40, 41, 42 },
    .global_channel = ENGINE_MIDI_OFF,
    .chroma_channel = 15,
    .drum_channel = 9,
};

volatile uint8_t lxr_velocity_always;

void engine_midi_setup(const struct engine_midi_setup *setup)
{
    uint8_t selected = midi_setup.selected;
    midi_setup = *setup;
    midi_setup.selected = selected;
    lxr_velocity_always = setup->velocity_always;
}

void engine_midi_setup_get(struct engine_midi_setup *setup) { *setup = midi_setup; }

void engine_select(unsigned channel) { midi_setup.selected = (uint8_t)(channel % ENGINE_CHANNELS); }
static uint16_t nrpn_number;
static volatile uint32_t midi_ring[MIDI_RING];
static volatile uint32_t midi_write, midi_read;
/* MIDI from the receive interrupt, with each message's arrival time */
static volatile struct { uint32_t message, cycles; } in_ring[MIDI_RING];
static volatile uint32_t in_write, in_read;
/* The time of the event being handled; an untimed one plays at the start
 * of the block */
static uint32_t event_cycles;
static int event_timed;

/* MIDI clock: the tempo is measured over each beat of 24 ticks, at the
 * audio block; without ticks for CLOCK_TIMEOUT_CYCLES it is the default */
#define TICKS_PER_BEAT      24u
#define CLOCK_TIMEOUT_CYCLES (2u * CPU_HZ)
#define SECONDS_PER_MINUTE  60.0f
static struct { uint32_t beat_start, last_tick; unsigned ticks; float bpm; int running; } clock_in;

float seq_getBpm(void) { return clock_in.bpm > 0.0f ? clock_in.bpm : DEFAULT_BPM; }

float engine_tempo(void) { return seq_getBpm(); }
int engine_clocked(void) { return clock_in.bpm > 0.0f; }

static void clock_tick(void)
{
    uint32_t now = event_timed ? event_cycles : cycles_now();
    if (now - clock_in.last_tick > CLOCK_TIMEOUT_CYCLES) { clock_in.ticks = 0; clock_in.beat_start = now; }
    clock_in.last_tick = now;
    if (++clock_in.ticks < TICKS_PER_BEAT) return;
    float seconds = (float)(now - clock_in.beat_start) / CPU_HZ;
    if (seconds > 0.0f) clock_in.bpm = SECONDS_PER_MINUTE / seconds;
    clock_in.ticks = 0;
    clock_in.beat_start = now;
}

static void clock_check(void)
{
    if (clock_in.bpm > 0.0f && cycles_now() - clock_in.last_tick > CLOCK_TIMEOUT_CYCLES) clock_in.bpm = 0.0f;
}

void seq_setMute(uint8_t voice, uint8_t isMuted)
{
    if (isMuted) muted |= 1u << voice;
    else muted &= ~(1u << voice);
}

static struct context *current;     /* the selected context */

static void select_context(unsigned channel)
{
    struct context *c = &contexts[channel];
    current = c;
    voiceArray = c->drums;
    lxr_snare = &c->snare;
    lxr_cymbal = &c->cymbal;
    lxr_hat = &c->hat;
    velocityModulators = c->velocity;
    parameterArray = c->parameters;
    activeParameters = c->values;
    mixer_decimation_rate = c->decimation_rate;
    mixer_audioRouting = c->routing;
}

/* The LXR voice (0-5) a channel's voice is; the open hi-hat is the hi-hat */
static unsigned lxr_voice(unsigned voice) { return voice < ENGINE_VOICES ? voice : ENGINE_VOICES - 1; }

/* A kit's LFO and velocity targets are indexes into modTargets. */
static void set_mod_targets(void)
{
    for (unsigned i = 0; i < ENGINE_VOICES; ++i) {
        uint8_t velocity = activeParameters[PAR_VEL_DEST_1 + i];
        uint8_t lfo = activeParameters[PAR_TARGET_LFO1 + i];
        modNode_setDestination(&velocityModulators[i],
                               modTargets[velocity < modTargetCount ? velocity : 0]);
        ModulationNode *node = i < NUM_DRUM_VOICES ? &voiceArray[i].lfo.modTarget :
                               i == 3 ? &snareVoice.lfo.modTarget :
                               i == 4 ? &cymbalVoice.lfo.modTarget : &hatVoice.lfo.modTarget;
        modNode_setDestination(node, modTargets[lfo < modTargetCount ? lfo : 0]);
    }
}

/* Sends parameter idx at its edit-buffer value through the LXR's CC handler,
 * with the LXR's numbering: CC idx + 1 below 128, CC2 idx - 128 above. */
static void send_parameter(uint16_t idx)
{
    MidiMsg msg = { 0 };
    if (idx < CC_PARAMETERS) { msg.status = MIDI_CC; msg.data1 = (idx + 1) & MIDI_DATA_MASK; }
    else { msg.status = CC_2; msg.data1 = (idx - CC_PARAMETERS) & MIDI_DATA_MASK; }
    msg.data2 = activeParameters[idx];
    midiParser_ccHandler(msg, 1, 0, 0.0f);
}

/* Sets the selected context's sound parameters; those past count are 0. */
static void load_parameters(const uint8_t *parameters, uint32_t count)
{
    if (count > END_OF_SOUND_PARAMETERS) count = END_OF_SOUND_PARAMETERS;
    memset(activeParameters, 0, END_OF_SOUND_PARAMETERS);
    memcpy(activeParameters, parameters, count);
    /* As the LXR-02 does on load: targets out of range are off */
    for (unsigned i = 0; i < ENGINE_VOICES; ++i) {
        if (activeParameters[PAR_VEL_DEST_1 + i] >= modTargetCount) activeParameters[PAR_VEL_DEST_1 + i] = 0;
        if (activeParameters[PAR_TARGET_LFO1 + i] >= modTargetCount) activeParameters[PAR_TARGET_LFO1 + i] = 0;
    }
    memcpy(current->kit, activeParameters, END_OF_SOUND_PARAMETERS);
    set_mod_targets();
    for (unsigned i = 0; i < END_OF_SOUND_PARAMETERS; ++i) {
        send_parameter(parameterOrder[i]);
        current->applied[parameterOrder[i]] = activeParameters[parameterOrder[i]];
    }
    if (current->has_morph) sweep_start();
}

void engine_init(void)
{
    __set_FPSCR(__get_FPSCR() | FPSCR_FLUSH_TO_ZERO);
    FxBus_init();
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) {
        select_context(c);
        for (unsigned i = 0; i <= ENGINE_VOICES; ++i) mixer_decimation_rate[i] = 1.0f;
        parameterArray_init();
        for (unsigned i = 0; i < ENGINE_VOICES; ++i) modNode_init(&velocityModulators[i]);
        initDrumVoice();
        Snare_init();
        HiHat_init();
        Cymbal_init();
        load_parameters(initKit + KIT_NAME_BYTES, sizeof initKit - KIT_NAME_BYTES);
        channels[c].voice = default_voice[c];
        channels[c].gain = 1.0f;
        channels[c].pitch_input = channels[c].accent_input = ENGINE_NO_INPUT;
    }
}

static void queue_message(uint32_t message)
{
    uint32_t w = midi_write;
    if (w - midi_read >= MIDI_RING) { engine_profile.midi_drops++; return; }
    midi_ring[w & (MIDI_RING - 1)] = message;
    __DMB();
    midi_write = w + 1;
}

void engine_midi(uint8_t status, uint8_t data1, uint8_t data2, uint32_t cycles)
{
    uint32_t w = in_write;
    if (w - in_read >= MIDI_RING) { engine_profile.midi_drops++; return; }
    in_ring[w & (MIDI_RING - 1)].message = status | data1 << 8 | (uint32_t)data2 << 16;
    in_ring[w & (MIDI_RING - 1)].cycles = cycles;
    __DMB();
    in_write = w + 1;
}

void engine_parameter(unsigned channel, uint16_t parameter, uint8_t value)
{
    queue_message(MESSAGE_PARAMETER | (uint32_t)parameter << MESSAGE_PARAMETER_SHIFT |
                  (uint32_t)(channel & MESSAGE_CHANNEL_MASK) << MESSAGE_CHANNEL_SHIFT |
                  (uint32_t)value << MESSAGE_VALUE_SHIFT);
}

uint8_t engine_parameter_value(unsigned channel, uint16_t parameter)
{
    return parameter < END_OF_SOUND_PARAMETERS ? contexts[channel].kit[parameter] : contexts[channel].values[parameter];
}

void engine_set_voice(unsigned channel, unsigned voice)
{
    queue_message(MESSAGE_VOICE | channel << 8 | voice << 16);
}

int engine_voice(unsigned channel)
{
    return channels[channel].voice == ENGINE_NO_VOICE ? -1 : channels[channel].voice;
}

/* The FX bus is shared; its parameters are set in every context. */
static int is_shared(unsigned parameter)
{
    return parameter >= PAR_FX_TYPE && parameter <= PAR_FX_DELAY_TYPE;
}

static int is_mod_target(unsigned parameter)
{
    return (parameter >= PAR_VEL_DEST_1 && parameter < PAR_VEL_DEST_1 + ENGINE_VOICES) ||
           (parameter >= PAR_TARGET_LFO1 && parameter < PAR_TARGET_LFO1 + ENGINE_VOICES);
}

/* As the LXR-02: routing and modulation targets never morph */
static int morphable(unsigned parameter)
{
    return !is_mod_target(parameter) &&
           !(parameter >= PAR_VOICE_LFO1 && parameter < PAR_VOICE_LFO1 + ENGINE_VOICES) &&
           !(parameter >= PAR_AUDIO_OUT1 && parameter < PAR_AUDIO_OUT1 + ENGINE_VOICES);
}

/* The LXR's interpolation, x in 1/256 steps, rounded */
static uint8_t interpolate(uint8_t a, uint8_t b, unsigned x)
{
    unsigned fixed = (unsigned)(a * 256 + (b - a) * (int)x);
    return (uint8_t)((fixed & 0xffu) < 0x7fu ? fixed >> 8 : (fixed >> 8) + 1);
}

/* A parameter's value before any CV route */
static uint8_t blended(const struct context *c, unsigned parameter)
{
    if (!c->has_morph || !morphable(parameter)) return c->kit[parameter];
    return interpolate(c->kit[parameter], c->morph[parameter], morph_amount);
}

static int is_hat(unsigned voice) { return voice == TRIGGER_HAT_CLOSED || voice == TRIGGER_HAT_OPEN; }

/* The LXR plays both hats on one voice, so a hit cuts the ringing one.
 * Here each channel has its own: a hat hit sets the other channels' hats
 * to their closed decay. */
static void choke_hats(unsigned channel)
{
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) {
        if (c == channel || !is_hat(channels[c].voice)) continue;
        select_context(c);
        hatVoice.oscVolEg.decay = hatVoice.decayClosed;
    }
}

/* The CV readings of the last block, for pitch and accent at a trigger */
static const float *trigger_cv;
#define SEMITONES_PER_VOLT  12.0f
#define NOTE_HIGHEST        127
#define VELOCITY_MAX        127.0f

/* The end of a channel's gate or note: a Gated channel fades out */
static void release(unsigned channel, unsigned hold) { channels[channel].held[hold / 32] &= ~(1u << hold % 32); }

static int is_held(const struct channel *ch)
{
    uint32_t any = 0;
    for (unsigned w = 0; w < HOLD_WORDS; ++w) any |= ch->held[w];
    return any != 0;
}

/* Lets go of every note, gate and button: Gated channels fade out */
static void release_all(void)
{
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c)
        for (unsigned w = 0; w < HOLD_WORDS; ++w) channels[c].held[w] = 0;
}

int engine_held(unsigned channel) { return is_held(&channels[channel]); }

int engine_sounding(unsigned channel) { return (sounding >> channel) & 1u; }

static void trigger(unsigned channel, unsigned hold, uint8_t note, uint8_t velocity)
{
    unsigned voice = channels[channel].voice;
    if (trigger_cv && channels[channel].pitch_input != ENGINE_NO_INPUT) {
        int shifted = note + (int)__builtin_lrintf(trigger_cv[channels[channel].pitch_input] * SEMITONES_PER_VOLT);
        note = (uint8_t)(shifted < 0 ? 0 : shifted > NOTE_HIGHEST ? NOTE_HIGHEST : shifted);
    }
    if (voice == ENGINE_NO_VOICE || (muted & (1u << channel))) return;
    struct channel *ch = &channels[channel];
    ch->held[hold / 32] |= 1u << hold % 32;
    /* Of two notes in one block, the later plays */
    if (ch->pending && ch->pending_timed &&
        (!event_timed || (int32_t)(event_cycles - ch->pending_cycles) < 0)) return;
    ch->pending = 1;
    ch->pending_note = note;
    ch->pending_velocity = velocity;
    ch->pending_timed = (uint8_t)event_timed;
    ch->pending_cycles = event_cycles;
}

/* Starts a channel's waiting note */
static void start_note(unsigned channel)
{
    struct channel *ch = &channels[channel];
    ch->pending = 0;
    if (!ch->mute) ch->gain = 1.0f;     /* a released channel plays at once */
    if (is_hat(ch->voice)) choke_hats(channel);
    select_context(channel);
    voiceControl_noteOn(ch->voice, ch->pending_note, ch->pending_velocity);
}

/* NRPN data: the LXR takes the value from CC 6 alone; a 14-bit sender
 * sends CC 6 and then CC 38 (the low 7 bits of 14). CC 6 waits up to
 * DATA_ENTRY_WAIT_BLOCKS for a CC 38, which is otherwise an LXR parameter. */
#define DATA_ENTRY_FINE         38u
#define DATA_ENTRY_WAIT_BLOCKS  3u      /* 2 ms */
#define PARAMETER_VALUE_MAX     255u
static struct { uint8_t pending, coarse, wait; } data_entry;

static void apply_control(uint8_t number, unsigned value, int wide);

static void flush_data_entry(void)
{
    if (!data_entry.pending) return;
    data_entry.pending = 0;
    apply_control(NRPN_DATA_ENTRY_COARSE, data_entry.coarse, 0);
}

static void data_entry_tick(void)
{
    if (data_entry.pending && !--data_entry.wait) flush_data_entry();
}

static void control_change(uint8_t number, uint8_t value)
{
    if (number == NRPN_DATA_ENTRY_COARSE) {
        flush_data_entry();
        data_entry = (typeof(data_entry)){ .pending = 1, .coarse = value, .wait = DATA_ENTRY_WAIT_BLOCKS };
        return;
    }
    if (number == DATA_ENTRY_FINE && data_entry.pending) {
        unsigned combined = (unsigned)data_entry.coarse << NRPN_NUMBER_BITS | value;
        data_entry.pending = 0;
        apply_control(NRPN_DATA_ENTRY_COARSE, combined > PARAMETER_VALUE_MAX ? PARAMETER_VALUE_MAX : combined, 1);
        return;
    }
    flush_data_entry();
    apply_control(number, value, 0);
}

static void set_parameter(unsigned channel, uint16_t parameter, uint8_t value);

/* A sound parameter goes to every channel through set_parameter, so the
 * morph blend applies; the morph amount takes 7 bits (scaled) or 14 */
static void apply_control(uint8_t number, unsigned value, int wide)
{
    int parameter = -1;
    if (number == NRPN_COARSE)
        nrpn_number = (nrpn_number & MIDI_DATA_MASK) | value << NRPN_NUMBER_BITS;
    else if (number == NRPN_FINE)
        nrpn_number = (nrpn_number & ~MIDI_DATA_MASK) | value;
    else if (number == NRPN_DATA_ENTRY_COARSE)
        parameter = CC_PARAMETERS + nrpn_number;
    else if (number > 0)
        parameter = number - 1;
    if (parameter == ENGINE_PARAMETER_MORPH) {
        morph_base = (uint8_t)(wide ? value : value * ENGINE_MORPH_MAX / MIDI_DATA_MASK);
        return;
    }
    if (parameter >= 0 && parameter < END_OF_SOUND_PARAMETERS) {
        for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) set_parameter(c, (uint16_t)parameter, (uint8_t)value);
        return;
    }
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) {
        select_context(c);
        MidiMsg msg = { .status = MIDI_CC, .data1 = number, .data2 = (uint8_t)value };
        midiParser_ccHandler(msg, 1, 0, 0.0f);
    }
}

static void send_value(uint16_t parameter, uint8_t value);

static void set_parameter(unsigned channel, uint16_t parameter, uint8_t value)
{
    select_context(channel);
    if (parameter >= END_OF_SOUND_PARAMETERS) { activeParameters[parameter] = value; send_parameter(parameter); return; }
    current->kit[parameter] = value;
    send_value(parameter, blended(current, parameter));
}

/* The Chroma 8 channel plays notes as the Assimil8or's Chroma 8 assignment
 * does: the block of eight keys picks the Assimil8or channel, the key in
 * the block the pitch. Channel 10 plays the General
 * MIDI drum notes at the kit's tuning, on the first channel playing that
 * voice. Control changes on any channel set the parameter on every
 * channel. */
/* Control changes are taken on any channel the setup uses */
static int listening(unsigned channel)
{
    const struct engine_midi_setup *m = &midi_setup;
    for (unsigned v = 0; v < ENGINE_TRIGGERS; ++v)
        if (m->voice_channel[v] == channel) return 1;
    return channel == m->global_channel || channel == m->chroma_channel || channel == m->drum_channel;
}

/* A note on (velocity above 0) plays the channels it maps to; a note off
 * releases them */
static void play(unsigned channel, unsigned hold, uint8_t key, uint8_t velocity)
{
    if (velocity) trigger(channel, hold, key, velocity);
    else release(channel, hold);
}

static void note(unsigned channel, uint8_t data1, uint8_t data2)
{
    const struct engine_midi_setup *m = &midi_setup;
    /* The LXR's: each voice on its channel, at one note or any */
    for (unsigned v = 0; v < ENGINE_TRIGGERS; ++v) {
        if (m->voice_channel[v] != channel) continue;
        if (m->voice_note[v] && m->voice_note[v] != data1) continue;
        uint8_t key = m->voice_note[v] ? SEQ_DEFAULT_NOTE : data1;
        for (unsigned c = 0; c < ENGINE_CHANNELS; ++c)
            if (channels[c].voice == v) play(c, data1, key, data2);
    }
    if (channel == m->global_channel) play(m->selected, data1, data1, data2);
    if (channel == m->chroma_channel && data1 >= CHROMA_FIRST_NOTE &&
        data1 < CHROMA_FIRST_NOTE + ENGINE_CHANNELS * CHROMA_BLOCK) {
        unsigned key = data1 - CHROMA_FIRST_NOTE;
        play(key / CHROMA_BLOCK, data1, SEQ_DEFAULT_NOTE + key % CHROMA_BLOCK - CHROMA_ROOT_KEY, data2);
    }
    if (channel == m->drum_channel)
        for (unsigned i = 0; i < sizeof drum_map / sizeof drum_map[0]; ++i)
            if (drum_map[i].note == data1)
                for (unsigned c = 0; c < ENGINE_CHANNELS; ++c)
                    if (channels[c].voice == drum_map[i].trigger) { play(c, data1, SEQ_DEFAULT_NOTE, data2); break; }
}

static void handle_midi(uint32_t message)
{
    uint8_t status = message, data1 = message >> 8, data2 = message >> 16;
    if (status == MESSAGE_PARAMETER) {
        uint16_t parameter = (message >> MESSAGE_PARAMETER_SHIFT) & MESSAGE_PARAMETER_MASK;
        unsigned channel = (message >> MESSAGE_CHANNEL_SHIFT) & MESSAGE_CHANNEL_MASK;
        uint8_t value = message >> MESSAGE_VALUE_SHIFT;
        if (parameter >= END_OF_SOUND_PARAMETERS) return;
        if (!is_shared(parameter)) set_parameter(channel, parameter, value);
        else for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) set_parameter(c, parameter, value);
        return;
    }
    if (status == MESSAGE_MIX) {
        if (data1 >= ENGINE_CHANNELS) return;
        uint8_t value = message >> MESSAGE_VALUE_SHIFT;
        if (data2 == ENGINE_MIX_MUTE) channels[data1].mute = value != 0;
        else if (data2 == ENGINE_MIX_SEND) channels[data1].send = value > ENGINE_SEND_MAX ? ENGINE_SEND_MAX : value;
        else if (data2 == ENGINE_MIX_GATED) channels[data1].gated = value != 0;
        return;
    }
    if (status == MESSAGE_RELEASE) {
        if (data1 < ENGINE_CHANNELS) release(data1, HOLD_BUTTON);
        return;
    }
    if (status == MESSAGE_VOICE) {
        if (data1 < ENGINE_CHANNELS && (data2 < ENGINE_TRIGGERS || data2 == ENGINE_NO_VOICE))
            channels[data1].voice = data2;
        return;
    }
    if (status == MIDI_CLOCK) { clock_tick(); return; }
    if (status == MIDI_START || status == MIDI_CONTINUE) { clock_in.running = 1; return; }
    if (status == MIDI_STOP) { clock_in.running = 0; return; }
    if (status == MESSAGE_AUDITION) {
        if (data1 < ENGINE_CHANNELS) trigger(data1, HOLD_BUTTON, SEQ_DEFAULT_NOTE, data2);
        return;
    }
    unsigned channel = status & MIDI_CHANNEL_MASK;
    switch (status & MIDI_TYPE_MASK) {
    case NOTE_OFF:
        note(channel, data1, 0);
        break;
    case NOTE_ON:
        note(channel, data1, data2);
        break;
    case MIDI_CC:
        if (!listening(channel)) break;
        /* Channel mode messages, not parameters */
        if (data1 == CC_ALL_SOUND_OFF || data1 == CC_ALL_NOTES_OFF) release_all();
        else control_change(data1, data2);
        break;
    default:
        break;
    }
}

/* From mixer.c: sample-and-hold at the voice's decimation rate. */
static void decimate(struct channel *ch, unsigned voice, int16_t *buffer)
{
    float increment = mixer_decimation_rate[voice] * mixer_decimation_rate[ENGINE_VOICES];
    if (increment == 1.0f) return;     /* full rate: every sample passes, the count stays */
    float count = ch->decimation_count;
    int16_t hold = ch->decimation_hold;
    for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
        count += increment;
        if (count >= 1.0f) { count -= 1.0f; hold = buffer[i]; }
        buffer[i] = hold;
    }
    ch->decimation_count = count;
    ch->decimation_hold = hold;
}

/* As mixer.c: adds a voice to an interleaved stereo buffer, saturating. */
static void add_stereo(int16_t *stereo, const int16_t *data, float left, float right)
{
    for (unsigned i = 0; i < AUDIO_BLOCK; i += 8) {
        float32x4_t lo, hi;
        neon_load(data + i, &lo, &hi);
        int16x8x2_t mix = vld2q_s16(stereo + 2 * i);
        int16x8_t l = vcombine_s16(vmovn_s32(vcvtq_s32_f32(vmulq_n_f32(lo, left))),
                                   vmovn_s32(vcvtq_s32_f32(vmulq_n_f32(hi, left))));
        int16x8_t r = vcombine_s16(vmovn_s32(vcvtq_s32_f32(vmulq_n_f32(lo, right))),
                                   vmovn_s32(vcvtq_s32_f32(vmulq_n_f32(hi, right))));
        mix.val[0] = vqaddq_s16(mix.val[0], l);
        mix.val[1] = vqaddq_s16(mix.val[1], r);
        vst2q_s16(stereo + 2 * i, mix);
    }
}

static void add_mono(int16_t *stereo, const int16_t *data, unsigned side)
{
    for (unsigned i = 0; i < AUDIO_BLOCK; i += 8) {
        int16x8x2_t mix = vld2q_s16(stereo + 2 * i);
        mix.val[side] = vqaddq_s16(mix.val[side], vld1q_s16(data + i));
        vst2q_s16(stereo + 2 * i, mix);
    }
}

/* Mute: the gain ramps to 0 or 1 across one block, so tails stop without
 * a click. A released Gated channel ramps to 0 over RELEASE_BLOCKS. */
static void fade(struct channel *ch, int16_t *data)
{
    float gain = ch->gain, end;
    if (ch->mute) end = 0.0f;
    else if (ch->gated && !is_held(ch)) end = gain > 1.0f / RELEASE_BLOCKS ? gain - 1.0f / RELEASE_BLOCKS : 0.0f;
    else end = 1.0f;
    if (gain == end) {
        if (end == 0.0f) memset(data, 0, AUDIO_BLOCK * sizeof *data);
        if (end == 0.0f || end == 1.0f) return;
    }
    float step = (end - gain) * (1.0f / AUDIO_BLOCK);
    for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
        gain += step;
        data[i] = (int16_t)(data[i] * gain);
    }
    ch->gain = end;
}



/* The Sample inputs: to the FX bus at input_send, to the mix at input_dry */
static volatile uint8_t input_send, input_dry;
#define INPUT_LEVEL_MAX     127.0f
#define SAMPLE_FULL_SCALE   32767.0f

void engine_inputs_audio(uint8_t send, uint8_t dry) { input_send = send; input_dry = dry; }

static void add_input_pair(int16_t *stereo, const struct audio_input *in, float gain)
{
    for (unsigned i = 0; i < AUDIO_BLOCK; i += 4) {
        int16x4x2_t frames = vld2_s16(stereo + 2 * i);
        for (unsigned side = 0; side < 2; ++side) {
            float32x4_t x = vmulq_n_f32(vld1q_f32(&in->sample[side][i]), gain);
            frames.val[side] = vqadd_s16(frames.val[side], vqmovn_s32(vcvtq_s32_f32(x)));
        }
        vst2_s16(stereo + 2 * i, frames);
    }
}

static void add_inputs(const struct audio_input *in, int16_t *mix, int16_t *fx)
{
    if (input_send) add_input_pair(fx, in, input_send * (SAMPLE_FULL_SCALE / INPUT_LEVEL_MAX));
    if (input_dry) add_input_pair(mix, in, input_dry * (SAMPLE_FULL_SCALE / INPUT_LEVEL_MAX));
}

static void route(unsigned voice, const int16_t *data, float left, float right,
                  int16_t *mix, int16_t *fx)
{
    switch (mixer_audioRouting[voice]) {
    case MIXER_ROUTING_DAC1_L: case MIXER_ROUTING_DAC2_L: add_mono(mix, data, 0); break;
    case MIXER_ROUTING_DAC1_R: case MIXER_ROUTING_DAC2_R: add_mono(mix, data, 1); break;
    case MIXER_ROUTING_FX_BUS: add_stereo(fx, data, left, right); break;
    default: add_stereo(mix, data, left, right); break;
    }
}

static void render_before_filter(unsigned voice, int16_t *data)
{
    if (voice < NUM_DRUM_VOICES) calcDrumVoiceSyncBlockPre(voice, data, AUDIO_BLOCK);
    else if (voice == 3) Snare_calcSyncBlockPre(data, AUDIO_BLOCK);
    else if (voice == 4) Cymbal_calcSyncBlockPre(data, AUDIO_BLOCK);
    else HiHat_calcSyncBlockPre(data, AUDIO_BLOCK);
}

static void render_after_filter(unsigned voice, int16_t *data)
{
    if (voice < NUM_DRUM_VOICES) calcDrumVoiceSyncBlockPost(voice, data, AUDIO_BLOCK);
    else if (voice == 3) Snare_calcSyncBlockPost(data, AUDIO_BLOCK);
    else if (voice == 4) Cymbal_calcSyncBlockPost(data, AUDIO_BLOCK);
    else HiHat_calcSyncBlockPost(data, AUDIO_BLOCK);
}

static ResonantFilter *voice_filter(unsigned voice, uint8_t *type)
{
    if (voice < NUM_DRUM_VOICES) { *type = voiceArray[voice].filterType; return &voiceArray[voice].filter; }
    if (voice == 3) { *type = snareVoice.filterType; return &snareVoice.filter; }
    if (voice == 4) { *type = cymbalVoice.filterType; return &cymbalVoice.filter; }
    *type = hatVoice.filterType;
    return &hatVoice.filter;
}

static void voice_pan(unsigned voice, float *left, float *right)
{
    if (voice < NUM_DRUM_VOICES) { *left = voiceArray[voice].triggeredPanL; *right = voiceArray[voice].triggeredPanR; }
    else if (voice == 3) { *left = snareVoice.triggeredPanL; *right = snareVoice.triggeredPanR; }
    else if (voice == 4) { *left = cymbalVoice.triggeredPanL; *right = cymbalVoice.triggeredPanR; }
    else { *left = hatVoice.triggeredPanL; *right = hatVoice.triggeredPanR; }
}

static void stage_done(unsigned stage, uint32_t *t)
{
    uint32_t now = cycles_now(), cycles = now - *t;
    *t = now;
    engine_profile.last[stage] = cycles;
    if (cycles > engine_profile.max[stage]) engine_profile.max[stage] = cycles;
}

/* The block-rate updates of the LXR mixer for one voice: its modulation
 * targets restored, then velocity and LFO modulation, filter coefficients
 * and envelopes. A second voice block in one output block reapplies the
 * LFO without moving it, so LFOs keep time with the output. */
static void voice_block(unsigned voice, int lfo_step)
{
    ModulationNode *velocity = &velocityModulators[voice];
    Lfo *lfo = voice < NUM_DRUM_VOICES ? &voiceArray[voice].lfo :
               voice == 3 ? &snareVoice.lfo : voice == 4 ? &cymbalVoice.lfo : &hatVoice.lfo;
    paramArray_setParameter(velocity->destination, velocity->originalValue);
    paramArray_setParameter(lfo->modTarget.destination, lfo->modTarget.originalValue);
    modNode_updateValue(velocity, velocity->lastVal);
    if (lfo_step) lfo_dispatchNextValue(lfo);
    else modNode_updateValue(&lfo->modTarget, lfo->modTarget.lastVal);
    uint8_t type;
    SVF_recalcFreq(voice_filter(voice, &type));
    if (voice < NUM_DRUM_VOICES) calcDrumVoiceAsync(voice);
    else if (voice == 3) Snare_calcAsync();
    else if (voice == 4) Cymbal_calcAsync();
    else HiHat_calcAsync();
}

/* Event timing. A timed event of the last block (from its start to this
 * block's) plays at its own frame of this block, so every timed note has a
 * latency of one block. */
#define BLOCK_CYCLES        (CPU_HZ / AUDIO_RATE * AUDIO_BLOCK)
static uint32_t window_start;

static unsigned frame_at(uint32_t cycles, uint32_t span)
{
    int32_t since = (int32_t)(cycles - window_start);
    if (since <= 0) return 0;
    uint32_t frame = (uint32_t)((uint64_t)(uint32_t)since * AUDIO_BLOCK / span);
    return frame < AUDIO_BLOCK ? frame : AUDIO_BLOCK - 1;
}

/* One voice block of the listed channels: each voice up to its filter, all
 * filters at once, then the rest of the voice, decimation and the fade.
 * t, if given, takes the stage times. */
static void render_voices(const unsigned *list, unsigned count, int16_t (*data)[AUDIO_BLOCK], uint32_t *t)
{
    int16_t *buffers[ENGINE_CHANNELS];
    ResonantFilter *filters[ENGINE_CHANNELS];
    uint8_t types[ENGINE_CHANNELS];
    for (unsigned k = 0; k < count; ++k) {
        unsigned v = lxr_voice(channels[list[k]].voice);
        select_context(list[k]);
        render_before_filter(v, data[k]);
        buffers[k] = data[k];
        filters[k] = voice_filter(v, &types[k]);
    }
    if (t) stage_done(STAGE_VOICES, t);
    filter_bank(filters, types, buffers, count);
    if (t) stage_done(STAGE_FILTERS, t);
    for (unsigned k = 0; k < count; ++k) {
        unsigned c = list[k], v = lxr_voice(channels[c].voice);
        select_context(c);
        render_after_filter(v, data[k]);
        decimate(&channels[c], v, data[k]);
        fade(&channels[c], data[k]);
    }
}

/* Puts a voice block at frame start of a channel's output; the part past
 * the end waits in carry for the next block */
static void place(struct channel *ch, int16_t *output, const int16_t *block, unsigned start)
{
    memcpy(output + start, block, (AUDIO_BLOCK - start) * sizeof *output);
    memcpy(ch->carry, block + AUDIO_BLOCK - start, start * sizeof *output);
    ch->phase = (uint8_t)start;
}

void engine_render(const struct audio_input *in, struct audio_output *out, uint32_t block_start)
{
    uint32_t t = cycles_now();
    take_load();
    data_entry_tick();
    clock_check();
    event_timed = 1;
    for (unsigned n = 0; n < MIDI_PER_BLOCK && in_read != in_write; ++n) {
        __DMB();
        event_cycles = in_ring[in_read & (MIDI_RING - 1)].cycles;
        handle_midi(in_ring[in_read & (MIDI_RING - 1)].message);
        in_read++;
    }
    event_timed = 0;
    for (unsigned n = 0; n < MIDI_PER_BLOCK && midi_read != midi_write; ++n) {
        __DMB();
        handle_midi(midi_ring[midi_read & (MIDI_RING - 1)]);
        midi_read++;
    }

    /* A note at or before a channel's phase starts before this block's voice
     * block and cuts the carried tail; a later one needs a second voice
     * block from its frame */
    uint32_t span = block_start - window_start;
    if (span < BLOCK_CYCLES / 2 || span > 2 * BLOCK_CYCLES) span = BLOCK_CYCLES;
    unsigned start[ENGINE_CHANNELS], list[ENGINE_CHANNELS], late[ENGINE_CHANNELS], count = 0, lates = 0;
    int16_t output[ENGINE_CHANNELS][AUDIO_BLOCK] __attribute__((aligned(16)));
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) {
        struct channel *ch = &channels[c];
        if (ch->voice == ENGINE_NO_VOICE) { ch->pending = 0; continue; }
        start[c] = ch->phase;
        if (ch->pending) {
            unsigned frame = ch->pending_timed ? frame_at(ch->pending_cycles, span) : 0;
            if (frame <= ch->phase) start_note(c);
            else late[lates++] = c;
            start[c] = frame;
        }
        memcpy(output[c], ch->carry, ch->phase * sizeof output[c][0]);
        list[count++] = c;
        select_context(c);
        voice_block(lxr_voice(ch->voice), 1);
    }
    stage_done(STAGE_BLOCK, &t);

    int16_t data[ENGINE_CHANNELS][AUDIO_BLOCK] __attribute__((aligned(16)));
    render_voices(list, count, data, &t);
    for (unsigned k = 0, l = 0; k < count; ++k) {
        unsigned c = list[k];
        int is_late = l < lates && late[l] == c;
        l += is_late;
        place(&channels[c], output[c], data[k], is_late ? channels[c].phase : start[c]);
    }
    if (lates) {
        for (unsigned k = 0; k < lates; ++k) {
            unsigned c = late[k];
            start_note(c);
            select_context(c);
            voice_block(lxr_voice(channels[c].voice), 0);
        }
        render_voices(late, lates, data, 0);
        for (unsigned k = 0; k < lates; ++k) {
            unsigned c = late[k];
            place(&channels[c], output[c], data[k], start[c]);
        }
    }
    window_start = block_start;

    int16_t mix[2 * AUDIO_BLOCK] __attribute__((aligned(16))) = { 0 };
    int16_t fx[2 * AUDIO_BLOCK] __attribute__((aligned(16))) = { 0 };
    add_inputs(in, mix, fx);
    memset(out->channel, 0, sizeof out->channel);
    uint8_t audible = 0;
    for (unsigned k = 0; k < count; ++k) {
        unsigned c = list[k], v = lxr_voice(channels[c].voice);
        int16_t *d = output[c];
        select_context(c);
        int peak = 0;
        for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
            out->channel[c][i] = d[i] * SAMPLE_SCALE;
            int a = d[i] < 0 ? -d[i] : d[i];
            if (a > peak) peak = a;
        }
        if (peak > SOUNDING_PEAK) audible |= (uint8_t)(1u << c);
        float left, right;
        voice_pan(v, &left, &right);
        route(v, d, left, right, mix, fx);
        if (channels[c].send && mixer_audioRouting[v] != MIXER_ROUTING_FX_BUS) {
            float send = channels[c].send * (1.0f / ENGINE_SEND_MAX);
            add_stereo(fx, d, left * send, right * send);
        }
    }
    sounding = audible;
    stage_done(STAGE_AFTER, &t);

    FxBus_process(fx, &mix[0], &mix[1], &mix[0], &mix[1], AUDIO_BLOCK);
    for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
        out->mix[0][i] = mix[2 * i] * SAMPLE_SCALE;
        out->mix[1][i] = mix[2 * i + 1] * SAMPLE_SCALE;
    }
    stage_done(STAGE_OUTPUT, &t);
}

/* The next free queue entry, waiting while the queue is full; 0 if it
 * stays full */
static struct load *load_slot(void)
{
    uint32_t start = cycles_now();
    while (load_write - load_read >= LOAD_QUEUE)
        if (cycles_now() - start > LOAD_WAIT_CYCLES) return 0;
    return &load_queue[load_write & (LOAD_QUEUE - 1)];
}

static void load_publish(void)
{
    __DMB();
    load_write++;
}

int engine_load_morph(unsigned channel, const uint8_t *parameters, uint32_t count)
{
    struct load *load = load_slot();
    if (!load) return 0;
    if (count > END_OF_SOUND_PARAMETERS) count = END_OF_SOUND_PARAMETERS;
    memcpy(load->parameters, parameters, count);
    load->count = count;
    load->channel = (uint8_t)channel;
    load->into_morph = 1;
    load_publish();
    return 1;
}

const uint8_t *engine_channel_morph(unsigned channel)
{
    return contexts[channel].has_morph ? contexts[channel].morph : 0;
}

void engine_channel_blend(unsigned channel, uint8_t *parameters)
{
    for (unsigned p = 0; p < END_OF_SOUND_PARAMETERS; ++p) parameters[p] = blended(&contexts[channel], p);
}

void engine_morph(uint8_t amount) { morph_base = amount; }
uint8_t engine_morph_value(void) { return morph_base; }
uint8_t engine_morph_amount(void) { return morph_amount; }

int engine_load_channel(unsigned channel, const uint8_t *parameters, uint32_t count,
                        unsigned voice, uint8_t mute, uint8_t send)
{
    struct load *load = load_slot();
    if (!load) return 0;
    if (count > END_OF_SOUND_PARAMETERS) count = END_OF_SOUND_PARAMETERS;
    memcpy(load->parameters, parameters, count);
    load->count = count;
    load->channel = (uint8_t)channel;
    load->voice = (uint8_t)voice;
    load->mute = mute;
    load->send = send;
    load->into_morph = 0;
    load_publish();
    return 1;
}

int engine_load_done(void) { return load_read == load_write; }

const uint8_t *engine_channel_parameters(unsigned channel) { return contexts[channel].kit; }

static void take_load(void)
{
    for (unsigned channels_loaded = 0; load_read != load_write && channels_loaded < LOADS_PER_BLOCK;) {
        __DMB();
        const struct load *load = &load_queue[load_read & (LOAD_QUEUE - 1)];
        select_context(load->channel);
        if (load->into_morph) {
            memset(current->morph, 0, sizeof current->morph);
            memcpy(current->morph, load->parameters, load->count);
            current->has_morph = load->count != 0;
            sweep_start();
        } else {
            load_parameters(load->parameters, load->count);
            struct channel *ch = &channels[load->channel];
            if (load->voice < ENGINE_TRIGGERS || load->voice == ENGINE_NO_VOICE) ch->voice = load->voice;
            ch->mute = load->mute;
            ch->send = load->send;
            channels_loaded++;
        }
        __DMB();
        load_read++;
    }
}

void engine_mix(unsigned channel, enum engine_mix_field field, uint8_t value)
{
    queue_message(MESSAGE_MIX | (channel & MESSAGE_CHANNEL_MASK) << 8 | (uint32_t)field << 16 |
                  (uint32_t)value << MESSAGE_VALUE_SHIFT);
}

uint8_t engine_mix_value(unsigned channel, enum engine_mix_field field)
{
    return field == ENGINE_MIX_MUTE ? channels[channel].mute :
           field == ENGINE_MIX_SEND ? channels[channel].send : channels[channel].gated;
}

int engine_cv_routes(const struct engine_cv_route *routes, unsigned count)
{
    if (cv_pending) return 0;
    unsigned idle = !cv_active;
    if (count > ENGINE_CV_ROUTES) count = ENGINE_CV_ROUTES;
    memcpy(cv_banks[idle], routes, count * sizeof *routes);
    cv_counts[idle] = count;
    __DMB();
    cv_pending = 1;
    return 1;
}

/* Puts a value in effect: the LXR code reads it from activeParameters */
static void send_value(uint16_t parameter, uint8_t value)
{
    if (parameter < END_OF_SOUND_PARAMETERS) current->applied[parameter] = value;
    activeParameters[parameter] = value;
    send_parameter(parameter);
    if (is_mod_target(parameter)) set_mod_targets();
}

void engine_inputs(const float *cv, uint8_t triggers, uint8_t timed, const uint32_t *cycles,
                   uint8_t releases)
{
    if (cv_pending) {
        /* Parameters routed before go back to the sweep, which resends them */
        const struct engine_cv_route *old = cv_banks[cv_active];
        for (unsigned r = 0; r < cv_counts[cv_active]; ++r)
            if (old[r].parameter < END_OF_SOUND_PARAMETERS) {
                contexts[old[r].channel].routed[old[r].parameter] = 0;
                contexts[old[r].channel].applied[old[r].parameter] = APPLIED_UNKNOWN;
            }
        cv_active = !cv_active;
        __DMB();
        cv_pending = 0;
        for (unsigned r = 0; r < ENGINE_CV_ROUTES; ++r) cv_applied[r] = -1;
        const struct engine_cv_route *routes = cv_banks[cv_active];
        for (unsigned r = 0; r < cv_counts[cv_active]; ++r)
            if (routes[r].parameter < END_OF_SOUND_PARAMETERS) contexts[routes[r].channel].routed[routes[r].parameter] = 1;
        sweep_start();
    }
    /* The morph amount first: routes on parameters build on the blend */
    const struct engine_cv_route *routes = cv_banks[cv_active];
    int amount = morph_base;
    for (unsigned r = 0; r < cv_counts[cv_active]; ++r)
        if (routes[r].parameter == ENGINE_PARAMETER_MORPH)
            amount += (int)__builtin_lrintf(cv[routes[r].input] * (routes[r].amount / CV_PERCENT / CV_FULL_SCALE_VOLTS) * MORPH_MAX);
    amount = amount < 0 ? 0 : amount > (int)MORPH_MAX ? (int)MORPH_MAX : amount;
    if (amount != morph_amount) { morph_amount = (uint8_t)amount; sweep_start(); }
    for (unsigned r = 0; r < cv_counts[cv_active]; ++r) {
        const struct engine_cv_route *route = &routes[r];
        if (route->parameter >= END_OF_SOUND_PARAMETERS) continue;
        int base = blended(&contexts[route->channel], route->parameter);
        float move = cv[route->input] * (route->amount / CV_PERCENT / CV_FULL_SCALE_VOLTS);
        int value = base + (int)__builtin_lrintf(move * (route->high - route->low));
        value = value < route->low ? route->low : value > route->high ? route->high : value;
        if (value == cv_applied[r]) continue;
        cv_applied[r] = (int16_t)value;
        select_context(route->channel);
        send_value(route->parameter, (uint8_t)value);
    }
    /* The sweep: the rest of the parameters, a slice a block */
    for (unsigned n = 0; n < SWEEP_PER_BLOCK && sweep_left; ++n, --sweep_left) {
        /* In the LXR's load order: some parameters depend on others set first */
        unsigned c = sweep_at / END_OF_SOUND_PARAMETERS, parameter = parameterOrder[sweep_at % END_OF_SOUND_PARAMETERS];
        sweep_at++;
        struct context *ctx = &contexts[c];
        if (ctx->routed[parameter]) continue;
        uint8_t value = blended(ctx, parameter);
        if (ctx->applied[parameter] == value) continue;
        select_context(c);
        send_value((uint16_t)parameter, value);
    }
    trigger_cv = cv;
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) {
        if (!(triggers >> c & 1u)) continue;
        uint8_t velocity = GATE_VELOCITY;
        if (channels[c].accent_input != ENGINE_NO_INPUT) {
            float level = cv[channels[c].accent_input] * (VELOCITY_MAX / CV_FULL_SCALE_VOLTS);
            velocity = (uint8_t)(level < 1.0f ? 1.0f : level > VELOCITY_MAX ? VELOCITY_MAX : level);
        }
        event_timed = timed >> c & 1u;
        event_cycles = cycles[c];
        trigger(c, HOLD_GATE, SEQ_DEFAULT_NOTE, velocity);
    }
    event_timed = 0;
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c)
        if (releases >> c & 1u) release(c, HOLD_GATE);
}

void engine_channel_inputs(unsigned channel, uint8_t pitch_input, uint8_t accent_input)
{
    channels[channel].pitch_input = pitch_input;
    channels[channel].accent_input = accent_input;
}

void engine_channel_inputs_get(unsigned channel, uint8_t *pitch_input, uint8_t *accent_input)
{
    *pitch_input = channels[channel].pitch_input;
    *accent_input = channels[channel].accent_input;
}

void engine_audition(unsigned channel, uint8_t velocity)
{
    queue_message(MESSAGE_AUDITION | (channel & MESSAGE_CHANNEL_MASK) << 8 | (uint32_t)velocity << 16);
}

void engine_release(unsigned channel)
{
    queue_message(MESSAGE_RELEASE | (channel & MESSAGE_CHANNEL_MASK) << 8);
}

void engine_profile_reset(void)
{
    for (unsigned i = 0; i < ENGINE_STAGES; ++i) engine_profile.max[i] = 0;
}
