/* Remote editing: the records an editor reads, the settings it changes, the
 * kits on the card and pictures of the waves. Each channel's sound is
 * addressed by role (its Decay, its Cutoff), which is the parameter of
 * whichever voice the channel plays, so one control works for any voice. */
#include "editor.h"
#include "engine.h"
#include "inputs.h"
#include "kits.h"
#include "settings.h"
#include "ui.h"
#include "waves.h"
#include "user_waves.h"
#include "editor_roles.h"
#include "Soundengine/DSPAudio/transientTables.h"
#include <stdio.h>
#include <string.h>

enum { OK, BAD_COMMAND, BAD_LENGTH, BAD_RANGE };

_Static_assert(END_OF_SOUND_PARAMETERS <= 255, "parameters fit a byte");

/* Records: a channel's sound and setup, its CV routes, its morph kit, the
 * kit's shared state, the user waves' names */
enum { RECORD_CHANNEL = 0, RECORD_ROUTES = 8, RECORD_MORPH = 16, RECORD_KIT = 24, RECORD_WAVES = 25 };
#define CHANNEL_BYTES       256u
#define CHANNEL_VOICE       248u    /* 0-6, CHANNEL_NO_VOICE for none */
#define CHANNEL_MUTE        249u
#define CHANNEL_SEND        250u
#define CHANNEL_GATED       251u
#define CHANNEL_PITCH       252u    /* 0 none, 1-3 the channel's A-C */
#define CHANNEL_ACCENT      253u
#define CHANNEL_HAS_MORPH   254u
#define CHANNEL_NO_VOICE    7u
_Static_assert(END_OF_SOUND_PARAMETERS < CHANNEL_VOICE, "a channel record starts with every sound parameter");
#define ROUTE_BYTES         2u      /* source, amount */
#define ROUTES_BYTES        (END_OF_SOUND_PARAMETERS * ROUTE_BYTES)
#define KIT_BYTES           256u
#define KIT_NAME_AT         32u
#define KIT_FOLDER_AT       80u
#define KIT_FOLDER_BYTES    128u
#define WAVES_BYTES         (2u * USER_WAVES * WAVES_NAME_BYTES)
#define TEMPO_SCALE         10.0f
#define AMOUNT_BIAS         100     /* a route amount travels as amount + 100 */
#define ROUTE_SOURCES       27      /* off, own A-C, 1A-8C */
#define ROUTE_AMOUNT_MAX    100

/* EDITOR_SET kinds; each takes a channel, an ID and a 16-bit value */
enum { SET_PARAMETER, SET_ROLE, SET_CHANNEL, SET_KIT, SET_ROUTE_SOURCE, SET_ROUTE_AMOUNT };
enum { FIELD_VOICE, FIELD_MUTE, FIELD_SEND, FIELD_GATED, FIELD_PITCH, FIELD_ACCENT };
enum { KIT_MORPH, KIT_MORPH_SOURCE, KIT_MORPH_AMOUNT, KIT_INPUT_SEND, KIT_INPUT_DRY, KIT_SELECTED };
#define SET_BYTES           7u
#define NIBBLE_BITS         4u
#define NIBBLES             4u
#define ALL_CHANNELS        0xffu
#define FIRST_TRANSIENT_SAMPLE 2u   /* Snap and Offset have no sample */
#define WAVE_HEADER_BYTES   4u

static uint32_t get_u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static void put_u16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void put_u32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static int clamp(int v, int low, int high) { return v < low ? low : v > high ? high : v; }

static uint8_t input_column(unsigned channel, uint8_t input)
{
    (void)channel;
    return input == ENGINE_NO_INPUT ? 0 : (uint8_t)(input / INPUTS_CHANNELS + 1u);
}

static void channel_record(unsigned c, uint8_t *r)
{
    memset(r, 0, CHANNEL_BYTES);
    memcpy(r, engine_channel_parameters(c), END_OF_SOUND_PARAMETERS);
    int voice = engine_voice(c);
    uint8_t pitch, accent;
    engine_channel_inputs_get(c, &pitch, &accent);
    r[CHANNEL_VOICE] = voice < 0 ? CHANNEL_NO_VOICE : (uint8_t)voice;
    r[CHANNEL_MUTE] = engine_mix_value(c, ENGINE_MIX_MUTE);
    r[CHANNEL_SEND] = engine_mix_value(c, ENGINE_MIX_SEND);
    r[CHANNEL_GATED] = engine_mix_value(c, ENGINE_MIX_GATED);
    r[CHANNEL_PITCH] = input_column(c, pitch);
    r[CHANNEL_ACCENT] = input_column(c, accent);
    r[CHANNEL_HAS_MORPH] = engine_channel_morph(c) != 0;
}

static void routes_record(unsigned c, uint8_t *r)
{
    const struct cv_route *routes = ui_cv_routes(c);
    for (unsigned p = 0; p < END_OF_SOUND_PARAMETERS; ++p) {
        r[p * ROUTE_BYTES] = (uint8_t)routes[p].source;
        r[p * ROUTE_BYTES + 1] = (uint8_t)routes[p].amount;
    }
}

static void morph_record(unsigned c, uint8_t *r)
{
    const uint8_t *morph = engine_channel_morph(c);
    if (morph) memcpy(r, morph, END_OF_SOUND_PARAMETERS);
    else memset(r, 0, END_OF_SOUND_PARAMETERS);
}

static void kit_record(uint8_t *r)
{
    struct engine_midi_setup m;
    engine_midi_setup_get(&m);
    memset(r, 0, KIT_BYTES);
    const struct cv_route *morph_route = ui_morph_route();
    r[0] = engine_morph_value();
    r[1] = engine_morph_amount();
    r[2] = (uint8_t)morph_route->source;
    r[3] = (uint8_t)morph_route->amount;
    put_u16(r + 4, (uint32_t)(engine_tempo() * TEMPO_SCALE + 0.5f));
    r[6] = (uint8_t)engine_clocked();
    r[7] = m.selected;
    r[8] = (uint8_t)waves_count(WAVES_OSC);
    r[9] = (uint8_t)waves_count(WAVES_TRANSIENT);
    r[10] = settings_input_level(SETTINGS_INPUT_SEND);
    r[11] = settings_input_level(SETTINGS_INPUT_DRY);
    memcpy(r + 12, m.voice_channel, ENGINE_TRIGGERS);
    memcpy(r + 12 + ENGINE_TRIGGERS, m.voice_note, ENGINE_TRIGGERS);
    r[26] = m.global_channel;
    r[27] = m.chroma_channel;
    r[28] = m.drum_channel;
    snprintf((char *)r + KIT_NAME_AT, KIT_FOLDER_AT - KIT_NAME_AT, "%s", ui_kit_name());
    snprintf((char *)r + KIT_FOLDER_AT, KIT_FOLDER_BYTES, "%s", ui_kits_folder());
}

static void waves_record(uint8_t *r)
{
    memset(r, 0, WAVES_BYTES);
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned n = 0; n < waves_count(kind); ++n)
            snprintf((char *)r + (kind * USER_WAVES + n) * WAVES_NAME_BYTES, WAVES_NAME_BYTES, "%s",
                     waves_name(kind, n));
}

/* A record into the buffer; its size, 0 for none */
static uint32_t record(unsigned which, uint8_t *r)
{
    if (which < RECORD_ROUTES) { channel_record(which, r); return CHANNEL_BYTES; }
    if (which < RECORD_MORPH) { routes_record(which - RECORD_ROUTES, r); return ROUTES_BYTES; }
    if (which < RECORD_KIT) { morph_record(which - RECORD_MORPH, r); return END_OF_SOUND_PARAMETERS; }
    if (which == RECORD_KIT) { kit_record(r); return KIT_BYTES; }
    if (which == RECORD_WAVES) { waves_record(r); return WAVES_BYTES; }
    return 0;
}

/* Fletcher-16: a changed sum marks the record to read again */
static uint32_t fletcher(const uint8_t *p, uint32_t bytes, uint32_t sum)
{
    uint32_t a = sum & 0xffu, b = sum >> 8;
    for (uint32_t i = 0; i < bytes; ++i) { a = (a + p[i]) % 255u; b = (b + a) % 255u; }
    return b << 8 | a;
}

static uint8_t buffer[WAVES_BYTES];

/* A sum for each channel (its sound, routes and morph kit), the kit's and
 * the waves' */
static uint32_t summary(uint8_t *out)
{
    for (unsigned c = 0; c < ENGINE_CHANNELS; ++c) {
        uint32_t sum = 0;
        for (unsigned which = c; which < RECORD_KIT; which += ENGINE_CHANNELS) {
            uint32_t bytes = record(which, buffer);
            sum = fletcher(buffer, bytes, sum);
        }
        put_u16(out + c * 2, sum);
    }
    put_u16(out + ENGINE_CHANNELS * 2, fletcher(buffer, record(RECORD_KIT, buffer), 0));
    put_u16(out + ENGINE_CHANNELS * 2 + 2, fletcher(buffer, record(RECORD_WAVES, buffer), 0));
    return ENGINE_CHANNELS * 2 + 4;
}

static void set_route(unsigned channel, uint16_t parameter, int source, int amount)
{
    unsigned owner = parameter >= PAR_FX_TYPE && parameter <= PAR_FX_DELAY_TYPE ? 0 : channel;
    struct cv_route *route = &ui_cv_routes(owner)[parameter];
    if (source >= 0) route->source = (int8_t)clamp(source, 0, ROUTE_SOURCES - 1);
    if (amount != INT32_MIN) route->amount = (int8_t)clamp(amount, -ROUTE_AMOUNT_MAX, ROUTE_AMOUNT_MAX);
    ui_cv_routes_changed();
}

static uint8_t input_of(unsigned channel, unsigned column)
{
    return column ? (uint8_t)inputs_cv_index(channel, clamp((int)column, 1, INPUTS_COLUMNS) - 1u) : ENGINE_NO_INPUT;
}

static int set(const uint8_t *p)
{
    unsigned channel = p[0], kind = p[1], id = p[2];
    int value = 0;
    for (unsigned n = 0; n < NIBBLES; ++n) value |= (p[3 + n] & 0xfu) << (n * NIBBLE_BITS);
    if (kind != SET_KIT && channel >= ENGINE_CHANNELS) return BAD_RANGE;
    switch (kind) {
    case SET_ROLE: {
        int voice = engine_voice(channel);
        if (id >= EDITOR_ROLES) return BAD_RANGE;
        if (voice < 0 || editor_roles[id].parameter[voice] == PAR_NONE) return OK;   /* nothing to set on this voice */
        id = editor_roles[id].parameter[voice];
        __attribute__((fallthrough));   /* to the voice's parameter */
    }
    case SET_PARAMETER: {
        if (id == PAR_NONE || id >= END_OF_SOUND_PARAMETERS) return BAD_RANGE;
        int low, high;
        ui_value_range((uint16_t)id, &low, &high);
        engine_parameter(channel, (uint16_t)id, (uint8_t)clamp(value, low, high));
        return OK;
    }
    case SET_CHANNEL: {
        uint8_t pitch, accent;
        switch (id) {
        case FIELD_VOICE: engine_set_voice(channel, value >= (int)ENGINE_TRIGGERS ? ENGINE_NO_VOICE : (unsigned)value); return OK;
        case FIELD_MUTE: engine_mix(channel, ENGINE_MIX_MUTE, value != 0); return OK;
        case FIELD_SEND: engine_mix(channel, ENGINE_MIX_SEND, (uint8_t)clamp(value, 0, ENGINE_SEND_MAX)); return OK;
        case FIELD_GATED: engine_mix(channel, ENGINE_MIX_GATED, value != 0); return OK;
        case FIELD_PITCH: case FIELD_ACCENT:
            engine_channel_inputs_get(channel, &pitch, &accent);
            if (id == FIELD_PITCH) pitch = input_of(channel, (unsigned)value);
            else accent = input_of(channel, (unsigned)value);
            engine_channel_inputs(channel, pitch, accent);
            return OK;
        default: return BAD_RANGE;
        }
    }
    case SET_KIT:
        switch (id) {
        case KIT_MORPH: engine_morph((uint8_t)clamp(value, 0, ENGINE_MORPH_MAX)); return OK;
        case KIT_MORPH_SOURCE: ui_morph_route()->source = (int8_t)clamp(value, 0, ROUTE_SOURCES - 1); ui_cv_routes_changed(); return OK;
        case KIT_MORPH_AMOUNT:
            ui_morph_route()->amount = (int8_t)clamp(value - AMOUNT_BIAS, -ROUTE_AMOUNT_MAX, ROUTE_AMOUNT_MAX);
            ui_cv_routes_changed();
            return OK;
        case KIT_INPUT_SEND: settings_set_input_level(SETTINGS_INPUT_SEND, (uint8_t)clamp(value, 0, SETTINGS_INPUT_MAX)); return OK;
        case KIT_INPUT_DRY: settings_set_input_level(SETTINGS_INPUT_DRY, (uint8_t)clamp(value, 0, SETTINGS_INPUT_MAX)); return OK;
        case KIT_SELECTED: engine_select((unsigned)clamp(value, 0, ENGINE_CHANNELS - 1)); return OK;
        default: return BAD_RANGE;
        }
    case SET_ROUTE_SOURCE: case SET_ROUTE_AMOUNT:
        if (id == PAR_NONE || id >= END_OF_SOUND_PARAMETERS) return BAD_RANGE;
        set_route(channel, (uint16_t)id, kind == SET_ROUTE_SOURCE ? value : -1,
                  kind == SET_ROUTE_AMOUNT ? value - AMOUNT_BIAS : INT32_MIN);
        return OK;
    default: return BAD_RANGE;
    }
}

/* A folder under KITS_FOLDER from a relative path; 0 when it would leave it */
static int kits_path(const uint8_t *p, uint32_t bytes, char *out, size_t size)
{
    char relative[KITS_PATH_BYTES];
    if (bytes >= sizeof relative) return 0;
    memcpy(relative, p, bytes);
    relative[bytes] = 0;
    if (strstr(relative, "..") || strchr(relative, ':')) return 0;
    if (!bytes) return snprintf(out, size, "%s", KITS_FOLDER) < (int)size;
    return snprintf(out, size, "%s/%s", KITS_FOLDER, relative) < (int)size;
}

/* The last folder listed, kept for the requests that page through it */
#define LISTED_MAX          128u
static struct { char path[KITS_PATH_BYTES]; struct kits_entry entries[LISTED_MAX]; unsigned count; int valid; } listed;

static uint32_t list(const uint8_t *p, uint32_t bytes, uint8_t *out, uint32_t out_max, int *status)
{
    char path[KITS_PATH_BYTES];
    if (bytes < 2 || !kits_path(p + 2, bytes - 2, path, sizeof path)) { *status = BAD_RANGE; return 0; }
    unsigned first = get_u16(p);
    if (!listed.valid || strcmp(path, listed.path) || !first) {
        if (!kits_folder()) { *status = BAD_RANGE; return 0; }
        snprintf(listed.path, sizeof listed.path, "%s", path);
        listed.count = kits_list(path, listed.entries, LISTED_MAX);
        listed.valid = 1;
    }
    put_u16(out, listed.count);
    uint32_t at = 3;
    uint8_t n = 0;
    for (unsigned i = first; i < listed.count; ++i) {
        size_t length = strlen(listed.entries[i].name);
        if (at + 2 + length > out_max) break;
        out[at++] = listed.entries[i].folder;
        memcpy(out + at, listed.entries[i].name, length + 1);
        at += (uint32_t)length + 1;
        n++;
    }
    out[2] = n;
    return at;
}

static int load(const uint8_t *p, uint32_t bytes)
{
    char path[KITS_PATH_BYTES];
    if (bytes < 3 || !kits_path(p + 2, bytes - 2, path, sizeof path)) return BAD_RANGE;
    int channel = p[0] == ALL_CHANNELS ? KITS_ALL_CHANNELS : p[0] < ENGINE_CHANNELS ? p[0] : -2;
    if (channel == -2 || !kits_load(path, channel, p[1])) return BAD_RANGE;
    if (channel == KITS_ALL_CHANNELS && !p[1]) {
        char *slash = strrchr(path, '/');
        *slash = 0;
        ui_kit_loaded(path, slash + 1);
    }
    return OK;
}

/* [kind: 0 .kit, 1 .snd] [channel] [blend] name; the reply is the file name */
static uint32_t save(const uint8_t *p, uint32_t bytes, uint8_t *out, uint32_t out_max, int *status)
{
    char name[KITS_KIT_NAME_BYTES + 1];
    if (bytes < 3 || bytes - 3 > KITS_KIT_NAME_BYTES || p[1] >= ENGINE_CHANNELS) { *status = BAD_LENGTH; return 0; }
    memset(name, ' ', KITS_KIT_NAME_BYTES);
    memcpy(name, p + 3, bytes - 3);
    name[KITS_KIT_NAME_BYTES] = 0;
    char saved[KITS_NAME_BYTES];
    int ok = p[0] ? kits_save_snd(p[1], name, p[2], saved, sizeof saved) : kits_save(name, p[2], saved, sizeof saved);
    if (!ok) { *status = BAD_RANGE; return 0; }
    listed.valid = 0;
    ui_kits_changed();
    size_t length = strlen(saved);
    if (length >= out_max) length = out_max - 1;
    memcpy(out, saved, length);
    return (uint32_t)length;
}

/* A wave's picture: its length in samples, then the lowest and highest
 * sample of each of `count` columns from `first`, of `columns` across it */
enum { WAVE_USER_OSC, WAVE_TRANSIENT };
#define WAVE_COLUMN_MAX     60u
static uint32_t wave(const uint8_t *p, uint32_t bytes, uint8_t *out, int *status)
{
    if (bytes != 5 || !p[2] || p[4] > WAVE_COLUMN_MAX || p[3] + p[4] > p[2]) { *status = BAD_LENGTH; return 0; }
    const int16_t *data16 = 0;
    const int8_t *data8 = 0;
    uint32_t length = 0;
    if (p[0] == WAVE_USER_OSC) {
        const int16_t (*tables)[USER_WAVE_POINTS] = p[1] < USER_WAVES ? user_wave(p[1]) : 0;
        if (tables) { data16 = tables[0]; length = USER_WAVE_POINTS; }
    } else if (p[0] == WAVE_TRANSIENT) {
        if (p[1] >= FIRST_TRANSIENT_SAMPLE && p[1] < FIRST_TRANSIENT_SAMPLE + NUM_TRANSIENTS) {
            data8 = transientData[p[1] - FIRST_TRANSIENT_SAMPLE];
            length = TRANSIENT_SAMPLE_LENGTH;
        } else if (p[1] >= FIRST_TRANSIENT_SAMPLE + NUM_TRANSIENTS)
            data16 = user_transient(p[1] - FIRST_TRANSIENT_SAMPLE - NUM_TRANSIENTS, &length);
    }
    if (!length) { *status = BAD_RANGE; return 0; }
    put_u32(out, length);
    for (unsigned c = 0; c < p[4]; ++c) {
        uint32_t from = (uint32_t)((uint64_t)(p[3] + c) * length / p[2]);
        uint32_t to = (uint32_t)((uint64_t)(p[3] + c + 1) * length / p[2]);
        if (to <= from) to = from + 1;
        int low = 127, high = -128;
        for (uint32_t i = from; i < to && i < length; ++i) {
            int v = data8 ? data8[i] : data16[i] >> 8;
            if (v < low) low = v;
            if (v > high) high = v;
        }
        out[WAVE_HEADER_BYTES + c * 2] = (uint8_t)(int8_t)low;
        out[WAVE_HEADER_BYTES + c * 2 + 1] = (uint8_t)(int8_t)high;
    }
    return WAVE_HEADER_BYTES + p[4] * 2u;
}

int editor_command(uint32_t command, const uint8_t *p, uint32_t bytes, uint8_t *out, uint32_t out_max,
                   uint32_t *count)
{
    int status = OK;
    *count = 0;
    switch (command) {
    case EDITOR_SUMMARY:
        if (bytes) return BAD_LENGTH;
        *count = summary(out);
        return OK;
    case EDITOR_READ: {
        /* [record] [offset, u16] [count] */
        if (bytes != 4) return BAD_LENGTH;
        uint32_t size = record(p[0], buffer), offset = get_u16(p + 1);
        if (!size || p[3] > out_max || offset + p[3] > size) return BAD_RANGE;
        memcpy(out, buffer + offset, p[3]);
        *count = p[3];
        return OK;
    }
    case EDITOR_SET:
        return bytes == SET_BYTES ? set(p) : BAD_LENGTH;
    case EDITOR_KITS:
        if (!bytes) return BAD_LENGTH;
        if (p[0] == EDITOR_KITS_LIST) *count = list(p + 1, bytes - 1, out, out_max, &status);
        else if (p[0] == EDITOR_KITS_LOAD) status = load(p + 1, bytes - 1);
        else if (p[0] == EDITOR_KITS_SAVE) *count = save(p + 1, bytes - 1, out, out_max, &status);
        else status = BAD_RANGE;
        return status;
    case EDITOR_WAVE:
        *count = wave(p, bytes, out, &status);
        return status;
    default:
        return -1;
    }
}
