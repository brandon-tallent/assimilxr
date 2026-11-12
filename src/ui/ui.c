/* The panel UI. Each Assimil8or channel plays an LXR voice; a section
 * button opens that section of the selected channel's voice, one level
 * deep. Pressing the same button again shows each parameter's CV route.
 *
 * Section buttons:  Pitch: Oscillator, Envelope: Amp envelope,
 * Loop Start: Modulation, Phase Mod: FM, Sample Start: Transient,
 * Sample End: Filter, Loop Mode: LFO, Level: Mix. Pan/Mix opens the
 * mixer (again: the FX bus), Channels the channel overview, Mutate
 * the mutate page. Play Mode switches the channel between 1 Shot and
 * Gated, as the Assimil8or's, and its LEDs show which.
 *
 * Sampling Setup sets the channel's pitch (1V/oct) and accent inputs.
 * Zones opens Morph: the amount between each channel's kit and its morph
 * kit, choosing and clearing the morph kit, and saving the blend.
 * Utilities lists MIDI settings, the Sample inputs' levels to the FX bus and mix, reloading
 * the user waves, display brightness, the CV monitor,
 * About, and Firmware: back to the stock firmware kept on the module, or
 * installing the app file on the front card.
 * On the channel overview the encoder steps through the kits in the last
 * folder browsed and a push loads the one shown.
 * Load browses the kits on the card (into all channels from the overview
 * or mixer, else into the selected channel); Save names a kit and writes
 * all eight channels, or the selected one as an LXR-02 kit.
 * A channel button selects the channel and plays it.
 * The encoder moves the cursor; pushing it starts and ends editing.
 * DATA 2 sets the selected value directly. */
#include "ui.h"
#include "panel.h"
#include "inputs.h"
#include "screen.h"
#include "engine.h"
#include "meters.h"
#include "lxr_menu.h"
#include "modTargets.h"
#include "graphs.h"
#include "names.h"
#include "kits.h"
#include "waves.h"
#include "settings.h"
#include "remote.h"
#include "install.h"
#include "storage.h"
#include "audio.h"
#include "cpu.h"
#include "display.h"
#include "kit_cv.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* White text; orange-red marks the selection, levels and pictures; amber
 * marks CV routes. */
#define FIRMWARE_NAME       "AssimiLXR"
#define FIRMWARE_AUTHOR     "Brandon Tallent"
#define GITHUB_HOST         "github.com/"   /* the GitHub address, wrapped after the host */
#define FIRMWARE_GITHUB     "brandon-tallent"
#define SPLASH_MS           2500u   /* the start-up screen, until a button or this long */
#define SPLASH_NAME_Y       6
#define ABOUT_GAP           4
#define FRONT_CARD          2u      /* storage_mounted(): a bit per drive, 1: is the front card */
#define SPLASH_TEXT_Y       34
#define SPLASH_AUTHOR_Y     108

#define WHITE               RGB565(255, 255, 255)
#define LABEL               RGB565(190, 190, 190)
#define GREY                RGB565(170, 170, 170)
#define OFF                 RGB565(40, 40, 40)
#define RULE                RGB565(56, 56, 56)
#define SELECTED            RGB565(34, 34, 34)
#define PANEL               RGB565(18, 18, 18)
#define ACCENT              RGB565(255, 80, 24)
#define AMBER               RGB565(255, 176, 0)
#define BLACK               0

#define HEADER_HEIGHT       18
#define BADGE_SIZE          16
#define ROW_TOP             20
#define ROW_HEIGHT          11
#define ROWS                8
#define FOOTER_TOP          117
#define GRAPH_GAP           5
#define GRAPH_MIN_HEIGHT    20
#define FOCUS_BAR_TOP       110
#define OVERLAY_HEIGHT      50
#define OVERLAY_MARGIN      3
#define OVERLAY_PAD         5
#define INSET               6
#define VALUE_RIGHT         126
#define ACCENT_WIDTH        2
#define SEGMENT_WIDTH       2
#define SEGMENT_PITCH       3
#define BAR_SEGMENTS        14
#define BAR_HEIGHT          4
#define DOT_SIZE            3
#define DOT_PITCH           5
#define CHANNELS            8
#define CELL_WIDTH          15
#define CELL_PITCH          16
#define METER_GAIN          2.0f
#define METER_SEGMENTS      16

#define SECTIONS            NUM_SUB_PAGES
#define SECTION_MIX         7u
#define DATA2_THRESHOLD     (1.5f / 127.0f)
#define MUTATE_DEFAULT      20
#define MUTATE_MAX          100
#define PERCENT             100
#define AUDITION_VELOCITY   100
#define COARSE_DIVISIONS    16      /* a pushed turn moves 1/16 of the range */
#define CV_COARSE_STEP      10

/* CV sources: off, the channel's own A, B, C, then inputs 1A-8C */
#define CV_OWN_INPUTS       3
#define CV_SOURCES          (1 + CV_OWN_INPUTS + CHANNELS * CV_OWN_INPUTS)
#define CV_AMOUNT_MAX       100
#define CV_AMOUNT_DEFAULT   50      /* given to a route when it gets a source */
#define CV_FIRST_SHARED     PAR_FX_TYPE     /* the FX bus parameters, shared by all channels */
#define CV_LAST_SHARED      PAR_FX_DELAY_TYPE

enum view { VIEW_CHANNELS, VIEW_SECTION, VIEW_FX, VIEW_MUTATE, VIEW_INSTRUMENT, VIEW_MIXER, VIEW_FILES, VIEW_SETTINGS,
           VIEW_UTILITIES, VIEW_DISPLAY, VIEW_MONITOR, VIEW_ABOUT, VIEW_MORPH, VIEW_CHANNEL, VIEW_AUDIO_IN, VIEW_SAVE, VIEW_FIRMWARE, VIEW_KIT_CV };
/* Mixer fields, top to bottom in each channel strip */
enum mix_field { MIX_LEVEL, MIX_PAN, MIX_SEND, MIX_MUTE, MIX_FIELDS };
enum edit { EDIT_NONE, EDIT_VALUE, EDIT_CV_SOURCE, EDIT_CV_AMOUNT };

static const struct { uint8_t button, section; } section_buttons[] = {
    { BUTTON_PITCH, 0 }, { BUTTON_ENVELOPE, 1 }, { BUTTON_LOOP_START, 2 },
    { BUTTON_PHASE_MOD, 3 }, { BUTTON_SAMPLE_START, 4 }, { BUTTON_SAMPLE_END, 5 },
    { BUTTON_LOOP_MODE, 6 }, { BUTTON_LEVEL, SECTION_MIX },
};
static const char *const voice_names[ENGINE_TRIGGERS] = {
    "Drum 1", "Drum 2", "Drum 3", "Snare", "Cymbal", "Hi-hat", "Open hat",
};

struct row { uint8_t name; uint16_t parameter; };

static struct {
    enum view view;
    enum edit edit;
    unsigned channel, section, cursor, cv_view;
    int mutate_amount, mutate_all;     /* mutate every channel, not the selected one */
    float data2_taken;
    struct row rows[ROWS];
    unsigned row_count;
    uint32_t now_ms, random;
    unsigned mix_field;
    int push_turned;        /* the encoder turned while pushed: no click on release */
    int push_editing;       /* editing began with a push-and-turn; ends on release */
    int splash_done;        /* the start-up screen has been dismissed */
    char message[40];       /* a short note shown over the page until message_until */
    uint32_t message_until;
} ui;

/* The Load browser: a folder under KITS_FOLDER, its entries, and where a
 * kit goes: all channels from the overview and mixer, else the selected one */
#define FILES_MAX           128u
#define FILES_PATH_BYTES    128u
#define MESSAGE_MS          1500u
static struct {
    char path[FILES_PATH_BYTES];
    struct kits_entry entries[FILES_MAX];
    unsigned count, cursor;
    int target, into_morph;
    enum view back;
} files;

/* The Channels page's quick browser: every kit in KITS_FOLDER and its
 * folders, in the Load browser's order, stepped by the encoder; a push
 * loads the one shown into all channels. folder is the current kit's
 * folder, which kit CV steps through. */
#define QUICK_KITS          1024u
#define QUICK_FOLDERS       96u
#define QUICK_DEPTH         4u
#define KIT_SAVE_DELAY_MS   2000u
struct quick_kit { char name[KITS_NAME_BYTES]; uint8_t folder; };
static struct {
    char folder[FILES_PATH_BYTES];
    char folders[QUICK_FOLDERS][FILES_PATH_BYTES];
    struct quick_kit kits[QUICK_KITS];
    unsigned count, folder_count, index;
    int listed, loaded;     /* loaded: index of the kit in the channels, or -1 */
    int cached;             /* the current folder's kits are in memory for kit CV */
    unsigned first, in_folder;  /* the current folder's kits in kits[] */
    uint32_t save_at;       /* the loaded kit is saved once loads settle */
    int save_pending;
} quick = { .folder = KITS_FOLDER, .loaded = -1 };

const char *ui_kits_folder(void) { return quick.folder; }

void ui_set_kits_folder(const char *folder)
{
    snprintf(quick.folder, sizeof quick.folder, "%s", folder);
    quick.listed = quick.cached = 0;
    quick.loaded = -1;
}

/* Each channel's routes; the shared FX bus parameters keep theirs on
 * channel 1, so "own" inputs there are channel 1's. */
static struct cv_route cv_routes[CHANNELS][END_OF_SOUND_PARAMETERS];
static int cv_routes_changed;

struct cv_route *ui_cv_routes(unsigned channel) { return cv_routes[channel]; }

static struct cv_route morph_route;
struct cv_route *ui_morph_route(void) { return &morph_route; }
void ui_cv_routes_changed(void) { cv_routes_changed = 1; }

static const char *row_name(const struct row *r)
{
    if (r->parameter == PAR_FX_TYPE) return "Effect";
    return ui_text_name(r->name);
}

/* Values: ranges and text by the LXR value type */
static int dtype(uint16_t parameter) { return parameter_dtypes[parameter] & 0xff; }
static int menu_of(uint16_t parameter) { return parameter_dtypes[parameter] >> 8; }

/* Waveform and transient menus continue with the card's user waves */
static int user_wave_kind(uint16_t parameter)
{
    if (dtype(parameter) != DTYPE_MENU) return -1;
    return menu_of(parameter) == LXR_MENU_WAVEFORM ? WAVES_OSC : menu_of(parameter) == LXR_MENU_TRANSIENT ? WAVES_TRANSIENT : -1;
}

static void value_range(uint16_t parameter, int *low, int *high)
{
    *low = 0;
    switch (dtype(parameter)) {
    case DTYPE_0B255: *high = 255; break;
    case DTYPE_MENU:
        *high = getMaxEntriesForMenu(menu_of(parameter)) - 1;
        if (user_wave_kind(parameter) >= 0) *high += (int)waves_count((unsigned)user_wave_kind(parameter));
        break;
    case DTYPE_ON_OFF: case DTYPE_0b1: *high = 1; break;
    case DTYPE_1B16: *low = 1; *high = 16; break;
    case DTYPE_0B15: *high = 15; break;
    case DTYPE_1B64: *low = 1; *high = 64; break;
    case DTYPE_0B100: *high = 100; break;
    case DTYPE_VOICE_LFO: *low = 1; *high = ENGINE_VOICES; break;
    case DTYPE_TARGET_SELECTION_LFO: case DTYPE_TARGET_SELECTION_VELO: *high = modTargetCount - 1; break;
    default: *high = 127; break;
    }
}

static int pitch_centred(uint16_t p)
{
    return p == PAR_COARSE1 || p == PAR_COARSE2 || p == PAR_COARSE3 || p == PAR_COARSE4 ||
           p == PAR_COARSE5 || p == PAR_COARSE6 || p == PAR_FM_FREQ1 || p == PAR_FM_FREQ2 ||
           p == PAR_FM_FREQ3 || p == PAR_MOD_OSC_F1_CYM || p == PAR_MOD_OSC_F2_CYM ||
           p == PAR_MOD_OSC_F1 || p == PAR_MOD_OSC_F2;
}

/* The name of a parameter as its page shows it */
static const char *row_name(const struct row *r);
static const char *target_name(uint16_t parameter)
{
    if (parameter == PAR_NONE) return "Off";
    for (unsigned page = 0; page < ENGINE_TRIGGERS; ++page)
        for (unsigned section = 0; section < NUM_SUB_PAGES; ++section) {
            const Page *p = &menuPages[page][section];
            for (unsigned k = 0; k < NUM_SUB_PAGES; ++k)
                if ((&p->bot1)[k] == parameter) {
                    struct row r = { (&p->top1)[k], parameter };
                    return row_name(&r);
                }
        }
    return "?";
}

#define COARSE_CENTRE       60
#define BIPOLAR_CENTRE      63

static void value_text(uint16_t parameter, uint8_t value, char *out, size_t size)
{
    char buf[8] = { 0 };
    switch (dtype(parameter)) {
    case DTYPE_MENU:
        if (user_wave_kind(parameter) >= 0 && value >= getMaxEntriesForMenu(menu_of(parameter))) {
            snprintf(out, size, "%s", waves_name((unsigned)user_wave_kind(parameter),
                                                 value - getMaxEntriesForMenu(menu_of(parameter))));
            char *dot = strrchr(out, '.');
            if (dot) *dot = 0;
            return;
        }
        getMenuItemNameForValue(menu_of(parameter), value, buf);
        if (ui_value_name(buf)) { snprintf(out, size, "%s", ui_value_name(buf)); return; }
        snprintf(out, size, "%s", buf);
        if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 'a' - 'A';
        return;
    case DTYPE_ON_OFF: snprintf(out, size, "%s", value ? "On" : "Off"); return;
    case DTYPE_PM63: {
        int centred = value - (pitch_centred(parameter) ? COARSE_CENTRE : BIPOLAR_CENTRE);
        snprintf(out, size, centred ? "%+d" : "0", centred);
        return;
    }
    case DTYPE_NOTE_NAME: setNoteName(value, buf); snprintf(out, size, "%s", buf); return;
    case DTYPE_TARGET_SELECTION_LFO: case DTYPE_TARGET_SELECTION_VELO:
        snprintf(out, size, "%s", target_name(modTargets[value < modTargetCount ? value : 0]));
        return;
    default: snprintf(out, size, "%u", value); return;
    }
}

/* Values with units, by the row's name; others as value_text */
#define PERIOD_BELOW_HZ     0.1f        /* slower rates show their period */
#define HUNDREDTHS_BELOW_HZ 10.0f
#define HZ_DECIMAL_BELOW    100.0f
#define KHZ                 1000.0f
#define KHZ_DECIMAL_BELOW   10000.0f
#define CENTS_RANGE         100.0f      /* fine tune spans +/-50 cents */
#define LFO_LOWEST          0.0078125f  /* the LXR's LFO rate law (lfo_setFreq) */
#define MIDI_MAX            127.0f
#define LFO_MAX_HZ          200.0f      /* LFO_MAX_F in lfo.h */
#define MS_PER_SECOND       1000u
#define A4_HZ               440.0f      /* the LXR's note table (MidiNoteFrequencies) */
#define A4_NOTE             69
#define SEMITONES           12.0f
static int row_value(uint8_t name, int fallback);

/* newlib's printf here has no floating point: fixed decimals by hand */
static void decimal_text(float v, unsigned places, const char *unit, char *out, size_t size)
{
    unsigned scale = places == 2 ? 100u : places == 1 ? 10u : 1u;
    unsigned n = (unsigned)(v * scale + 0.5f);
    if (places) snprintf(out, size, "%u.%0*u %s", n / scale, (int)places, n % scale, unit);
    else snprintf(out, size, "%u %s", n, unit);
}

static void hz_text(float hz, char *out, size_t size)
{
    if (hz < PERIOD_BELOW_HZ) snprintf(out, size, "%u s", (unsigned)(1.0f / hz + 0.5f));
    else if (hz < HUNDREDTHS_BELOW_HZ) decimal_text(hz, 2, "Hz", out, size);
    else if (hz < HZ_DECIMAL_BELOW) decimal_text(hz, 1, "Hz", out, size);
    else if (hz < KHZ) decimal_text(hz, 0, "Hz", out, size);
    else if (hz < KHZ_DECIMAL_BELOW) decimal_text(hz / KHZ, 2, "kHz", out, size);
    else decimal_text(hz / KHZ, 1, "kHz", out, size);
}

static void ms_text(unsigned ms, char *out, size_t size)
{
    if (ms >= MS_PER_SECOND) snprintf(out, size, "%u.%02u s", ms / MS_PER_SECOND, ms % MS_PER_SECOND / 10);
    else snprintf(out, size, "%u ms", ms);
}

static void row_value_text(const struct row *r, uint8_t value, char *out, size_t size)
{
    switch (r->name) {
    case TEXT_COARSE:
    {   /* the note, detuned linearly by up to a semitone as midiParser_calcDetune */
        float fine = engine_parameter_value(ui.channel, r->parameter + 1) / MIDI_MAX - 0.5f;
        float semitone = powf(2.0f, 1.0f / SEMITONES);
        hz_text(A4_HZ * powf(2.0f, (value - A4_NOTE) / SEMITONES) * (1.0f + fine * (semitone - 1.0f)), out, size);
        }
        return;
    case TEXT_FINE: {
        int cents = (int)__builtin_lrintf((value / MIDI_MAX - 0.5f) * CENTS_RANGE);
        snprintf(out, size, cents ? "%+d ct" : "0 ct", cents);
        return;
    }
    case TEXT_FILTER_F: hz_text(graph_cutoff_hz(value), out, size); return;
    case TEXT_ATTACK:
        ms_text(graph_envelope_ms(value, row_value(TEXT_DECAY, row_value(TEXT_DECAY_CLOSED, 0)),
                                  row_value(TEXT_EG_SLOPE, 0), 0), out, size);
        return;
    case TEXT_DECAY: case TEXT_DECAY_CLOSED: case TEXT_DECAY_OPEN:
        ms_text(graph_envelope_ms(row_value(TEXT_ATTACK, 0), value, row_value(TEXT_EG_SLOPE, 0), 1), out, size);
        return;
    case TEXT_PITCH_DECAY: ms_text(graph_decay_ms(value, row_value(TEXT_EG_SLOPE, 0)), out, size); return;
    case TEXT_FREQ_LFO:
        if (!row_value(TEXT_SYNC_LFO, 0)) {
            float f = LFO_LOWEST + value / MIDI_MAX * (1.0f - LFO_LOWEST);
            hz_text(f * f * f * LFO_MAX_HZ, out, size);
            return;
        }
        break;
    case TEXT_PAN: {
        int centred = value - BIPOLAR_CENTRE;
        if (!centred) snprintf(out, size, "C");
        else snprintf(out, size, "%c%d", centred < 0 ? 'L' : 'R', centred < 0 ? -centred : centred);
        return;
    }
    default: break;
    }
    value_text(r->parameter, value, out, size);
}

static void cv_source_text(int source, char *out, size_t size)
{
    static const char own[CV_OWN_INPUTS] = { 'A', 'B', 'C' };
    if (source <= 0) snprintf(out, size, "--");
    else if (source <= CV_OWN_INPUTS) snprintf(out, size, "%c", own[source - 1]);
    else {
        int input = source - 1 - CV_OWN_INPUTS;
        snprintf(out, size, "%d%c", input / CV_OWN_INPUTS + 1, own[input % CV_OWN_INPUTS]);
    }
}

void ui_value_range(uint16_t parameter, int *low, int *high) { value_range(parameter, low, high); }

/* The rows of the open page: its sound parameters, in LXR order */
static int voice(void) { return engine_voice(ui.channel); }

static void build_rows(void)
{
    ui.row_count = 0;
    const Page *page;
    if (ui.view == VIEW_FX) page = &menuPages[FX_MODE_PAGE][engine_parameter_value(ui.channel, PAR_FX_TYPE)];
    else if (ui.view == VIEW_SECTION && voice() >= 0) page = &menuPages[voice()][ui.section];
    else return;
    const uint8_t *names = &page->top1;
    const uint16_t *parameters = &page->bot1;
    for (unsigned k = 0; k < ROWS; ++k) {
        if (names[k] == TEXT_EMPTY || names[k] == TEXT_SKIP || parameters[k] == PAR_NONE ||
            parameters[k] >= END_OF_SOUND_PARAMETERS) continue;
        ui.rows[ui.row_count++] = (struct row){ names[k], parameters[k] };
    }
    if (ui.cursor >= ui.row_count) ui.cursor = ui.row_count ? ui.row_count - 1 : 0;
}

static int clamp(int v, int low, int high) { return v < low ? low : v > high ? high : v; }

static unsigned route_channel(unsigned channel, uint16_t parameter)
{
    return parameter >= CV_FIRST_SHARED && parameter <= CV_LAST_SHARED ? 0 : channel;
}

static struct cv_route *route_of(uint16_t parameter)
{
    return &cv_routes[route_channel(ui.channel, parameter)][parameter];
}

/* The CV input a source selects for a channel */
static unsigned cv_input(unsigned channel, int source)
{
    if (source <= CV_OWN_INPUTS) return inputs_cv_index(channel, (unsigned)source - 1);
    unsigned input = (unsigned)source - 1 - CV_OWN_INPUTS;
    return inputs_cv_index(input / CV_OWN_INPUTS, input % CV_OWN_INPUTS);
}

/* A CV reading as "+1.25 V" (no float printf in newlib-nano) */
#define CENTIVOLTS          100.0f
static void volts_text(float volts, char *out, size_t size)
{
    int cv = (int)__builtin_lrintf(volts * CENTIVOLTS), magnitude = cv < 0 ? -cv : cv;
    snprintf(out, size, "%c%d.%02d V", cv < 0 ? '-' : '+', magnitude / (int)CENTIVOLTS, magnitude % (int)CENTIVOLTS);
}

/* Sends the routes with a source and an amount to the engine */
static void publish_cv_routes(void)
{
    static struct engine_cv_route list[ENGINE_CV_ROUTES];
    unsigned count = 0;
    for (unsigned c = 0; c < CHANNELS; ++c)
        for (uint16_t p = 0; p < END_OF_SOUND_PARAMETERS && count < ENGINE_CV_ROUTES; ++p) {
            const struct cv_route *cv = &cv_routes[c][p];
            if (!cv->source || !cv->amount) continue;
            int low, high;
            value_range(p, &low, &high);
            list[count++] = (struct engine_cv_route){
                .parameter = p, .channel = (uint8_t)c, .input = (uint8_t)cv_input(c, cv->source),
                .low = (uint8_t)low, .high = (uint8_t)high, .amount = cv->amount,
            };
        }
    if (morph_route.source && morph_route.amount && count < ENGINE_CV_ROUTES)
        list[count++] = (struct engine_cv_route){
            .parameter = ENGINE_PARAMETER_MORPH, .channel = 0, .input = (uint8_t)cv_input(0, morph_route.source),
            .low = 0, .high = ENGINE_MORPH_MAX, .amount = morph_route.amount,
        };
    if (engine_cv_routes(list, count)) cv_routes_changed = 0;
}

static void set_value(uint16_t parameter, int value)
{
    int low, high;
    value_range(parameter, &low, &high);
    engine_parameter(ui.channel, parameter, (uint8_t)clamp(value, low, high));
}

/* Mixer: level and pan are the voice's own LXR settings; send and mute
 * belong to the channel */
static const uint16_t level_parameters[ENGINE_VOICES] = { PAR_VOL1, PAR_VOL2, PAR_VOL3, PAR_VOL4, PAR_VOL5, PAR_VOL6 };
static const uint16_t pan_parameters[ENGINE_VOICES] = { PAR_PAN1, PAR_PAN2, PAR_PAN3, PAR_PAN4, PAR_PAN5, PAR_PAN6 };
static const char *const mix_names[MIX_FIELDS] = { "Level", "Pan", "FX send", "Mute" };

/* The LXR voice of a channel (the open hat is the hat), or -1 */
static int lxr_voice_of(unsigned channel)
{
    int t = engine_voice(channel);
    return t < 0 ? -1 : t < (int)ENGINE_VOICES ? t : (int)ENGINE_VOICES - 1;
}

static uint16_t mix_parameter(unsigned channel, unsigned field)
{
    int v = lxr_voice_of(channel);
    if (v < 0) return PAR_NONE;
    return field == MIX_LEVEL ? level_parameters[v] : pan_parameters[v];
}

static void mix_range(unsigned field, int *low, int *high)
{
    *low = 0;
    *high = field == MIX_MUTE ? 1 : field == MIX_SEND ? (int)ENGINE_SEND_MAX : 0;
    if (field == MIX_LEVEL || field == MIX_PAN) value_range(field == MIX_LEVEL ? PAR_VOL1 : PAR_PAN1, low, high);
}

/* A field's value, or -1 for a channel without a voice */
static int mix_value(unsigned channel, unsigned field)
{
    if (field == MIX_SEND) return engine_mix_value(channel, ENGINE_MIX_SEND);
    if (field == MIX_MUTE) return engine_mix_value(channel, ENGINE_MIX_MUTE);
    uint16_t p = mix_parameter(channel, field);
    return p == PAR_NONE ? -1 : engine_parameter_value(channel, p);
}

static void mix_set(unsigned channel, unsigned field, int value)
{
    int low, high;
    mix_range(field, &low, &high);
    value = clamp(value, low, high);
    if (field == MIX_SEND) engine_mix(channel, ENGINE_MIX_SEND, (uint8_t)value);
    else if (field == MIX_MUTE) engine_mix(channel, ENGINE_MIX_MUTE, (uint8_t)value);
    else if (mix_parameter(channel, field) != PAR_NONE)
        engine_parameter(channel, mix_parameter(channel, field), (uint8_t)value);
}

static void mix_text(unsigned channel, unsigned field, char *out, size_t size)
{
    int v = mix_value(channel, field);
    if (v < 0) snprintf(out, size, "--");
    else if (field == MIX_MUTE) snprintf(out, size, "%s", v ? "Muted" : "Playing");
    else if (field == MIX_PAN) {
        int centred = v - BIPOLAR_CENTRE;
        if (!centred) snprintf(out, size, "Centre");
        else snprintf(out, size, "%c%d", centred < 0 ? 'L' : 'R', centred < 0 ? -centred : centred);
    }
    else snprintf(out, size, "%d", v);
}

static int coarse(int low, int high)
{
    int step = (high - low + COARSE_DIVISIONS / 2) / COARSE_DIVISIONS;
    return step > 1 ? step : 1;
}

/* MIDI settings: how velocity sets the level, two rows a voice (channel,
 * note), then the global, Chroma 8 and drum channels. Saved to the card
 * when the page is left. */
enum { SET_VELOCITY, SET_VOICES, SET_GLOBAL = SET_VOICES + 2 * ENGINE_TRIGGERS, SET_CHROMA, SET_DRUMS,
       SETTINGS_ROWS };
#define MIDI_CHANNEL_COUNT  16
#define NOTE_ANY            0
#define NOTE_HIGHEST        127
static struct { unsigned cursor; int dirty; } settings_page;

static int setting_is_voice(unsigned row) { return row >= SET_VOICES && row < SET_GLOBAL; }
static int setting_is_note(unsigned row) { return setting_is_voice(row) && ((row - SET_VOICES) & 1u); }

static uint8_t *setting_of(struct engine_midi_setup *m, unsigned row)
{
    unsigned voice = (row - SET_VOICES) / 2;
    if (setting_is_voice(row)) return setting_is_note(row) ? &m->voice_note[voice] : &m->voice_channel[voice];
    return row == SET_VELOCITY ? &m->velocity_always : row == SET_GLOBAL ? &m->global_channel :
           row == SET_CHROMA ? &m->chroma_channel : &m->drum_channel;
}

/* Channels step Off, 1-16 */
static void setting_step(unsigned row, int steps)
{
    struct engine_midi_setup m;
    engine_midi_setup_get(&m);
    uint8_t *v = setting_of(&m, row);
    if (setting_is_note(row)) *v = (uint8_t)clamp(*v + steps, NOTE_ANY, NOTE_HIGHEST);
    else if (row == SET_VELOCITY) *v = steps > 0;
    else {
        int position = *v == ENGINE_MIDI_OFF ? 0 : *v + 1;
        position = clamp(position + steps, 0, MIDI_CHANNEL_COUNT);
        *v = position ? (uint8_t)(position - 1) : ENGINE_MIDI_OFF;
    }
    engine_midi_setup(&m);
    settings_page.dirty = 1;
}

static void setting_text(unsigned row, char *name, size_t name_size, char *value, size_t value_size)
{
    static const char *const extra[] = { "Global channel", "Chroma 8 channel", "GM drum channel" };
    struct engine_midi_setup m;
    engine_midi_setup_get(&m);
    uint8_t v = *setting_of(&m, row);
    if (row == SET_VELOCITY) {
        snprintf(name, name_size, "Velocity");
        snprintf(value, value_size, "%s", v ? "Global" : "Kit");
        return;
    }
    if (setting_is_voice(row))
        snprintf(name, name_size, "%s %s", voice_names[(row - SET_VOICES) / 2], setting_is_note(row) ? "note" : "ch");
    else snprintf(name, name_size, "%s", extra[row - SET_GLOBAL]);
    if (setting_is_note(row)) {
        if (v == NOTE_ANY) snprintf(value, value_size, "Any");
        else { char note[8] = { 0 }; setNoteName(v, note); snprintf(value, value_size, "%s", note); }
    } else if (v == ENGINE_MIDI_OFF) snprintf(value, value_size, "Off");
    else snprintf(value, value_size, "%u", v + 1u);
}

/* Utilities: a list of pages, and a restart into the stock firmware that
 * a second push confirms */
enum { UTILITY_MIDI, UTILITY_KIT_CV, UTILITY_AUDIO_IN, UTILITY_WAVES, UTILITY_DISPLAY, UTILITY_MONITOR, UTILITY_ABOUT,
       UTILITY_FIRMWARE, UTILITIES };
static const char *const utility_names[UTILITIES] = {
    "MIDI", "Kit CV", "Audio inputs", "Reload waves", "Display", "CV monitor", "About", "Firmware",
};

/* Kit CV: an input for each role, Off or 1A-8C */
static const char *const kit_cv_names[KIT_CV_ROLES] = { "Next kit", "Previous kit", "First kit", "Select kit" };
static unsigned kit_cv_cursor;
#define KIT_CV_CHOICES      (1 + CHANNELS * INPUTS_COLUMNS)     /* Off, then 1A-8C */

static int kit_cv_choice(uint8_t input)
{
    for (unsigned k = 1; k < KIT_CV_CHOICES; ++k)
        if (inputs_cv_index((k - 1) / INPUTS_COLUMNS, (k - 1) % INPUTS_COLUMNS) == input) return (int)k;
    return 0;
}

static void kit_cv_step(int steps)
{
    enum kit_cv_role role = (enum kit_cv_role)kit_cv_cursor;
    int k = clamp(kit_cv_choice(kit_cv_input(role)) + steps, 0, KIT_CV_CHOICES - 1);
    kit_cv_set_input(role, k ? (uint8_t)inputs_cv_index((unsigned)(k - 1) / INPUTS_COLUMNS,
                                                        (unsigned)(k - 1) % INPUTS_COLUMNS) : KIT_CV_OFF);
}

/* Firmware: back to the stock firmware kept on the module, or the app file
 * on the front card, as Load Software installs it; each confirmed by a
 * second push, then a restart */
enum { FIRMWARE_STOCK, FIRMWARE_CARD, FIRMWARE_ROWS };
static struct { unsigned cursor; int confirming; } firmware_page;
static unsigned audio_in_cursor;
static struct { unsigned cursor; int confirming; } utilities;

/* The channel's inputs: a pitch input (1V/oct) and an accent input, each
 * off or one of the channel's own A-C */
enum { CHANNEL_PITCH, CHANNEL_ACCENT, CHANNEL_ROWS };
static unsigned channel_cursor;

static int channel_column(unsigned row)
{
    uint8_t pitch, accent;
    engine_channel_inputs_get(ui.channel, &pitch, &accent);
    uint8_t input = row == CHANNEL_PITCH ? pitch : accent;
    return input == ENGINE_NO_INPUT ? 0 : (int)(input / INPUTS_CHANNELS) + 1;
}

static void channel_step(unsigned row, int steps)
{
    int column = clamp(channel_column(row) + steps, 0, INPUTS_COLUMNS);
    uint8_t pitch, accent;
    engine_channel_inputs_get(ui.channel, &pitch, &accent);
    uint8_t input = column ? (uint8_t)inputs_cv_index(ui.channel, (unsigned)column - 1u) : ENGINE_NO_INPUT;
    if (row == CHANNEL_PITCH) pitch = input; else accent = input;
    engine_channel_inputs(ui.channel, pitch, accent);
}

/* Morph: the amount, the morph kit (chosen in the browser), clearing it
 * and saving the blend as a kit. Pressed again, the amount's CV route. */
enum { MORPH_AMOUNT, MORPH_KIT, MORPH_CLEAR, MORPH_SAVE, MORPH_ROWS };
static const char *const morph_rows[MORPH_ROWS] = { "Amount", "Morph kit", "Clear morph", "Save blend" };
#define MORPH_COARSE_STEP   16
static struct { unsigned cursor; char kit[KITS_NAME_BYTES]; } morph_page;

static char fault[32];
void ui_fault(const char *text) { snprintf(fault, sizeof fault, "%s", text); }

static void show_message(const char *text)
{
    snprintf(ui.message, sizeof ui.message, "%s", text);
    ui.message_until = ui.now_ms + MESSAGE_MS;
}

static int files_nested(void) { return strcmp(files.path, KITS_FOLDER) != 0; }

static void files_list(void)
{
    files.count = kits_list(files.path, files.entries, FILES_MAX);
    files.cursor = 0;
}

static void open_view(enum view view);
static void quick_follow(const char *folder, const char *name);

static void open_files(int into_morph)
{
    if (!kits_folder()) { show_message("No card"); return; }
    files.back = ui.view;
    files.into_morph = into_morph;
    files.target = ui.view == VIEW_CHANNELS || ui.view == VIEW_MIXER || ui.view == VIEW_MORPH ?
                   KITS_ALL_CHANNELS : (int)ui.channel;
    snprintf(files.path, sizeof files.path, "%s", KITS_FOLDER);
    files_list();
    open_view(VIEW_FILES);
}

/* The rows: ".." first inside a subfolder, then the entries */
static unsigned files_rows(void) { return files.count + (files_nested() ? 1u : 0u); }

static void files_push(void)
{
    if (!files_rows()) return;
    unsigned i = files.cursor;
    if (files_nested()) {
        if (i == 0) {
            *strrchr(files.path, '/') = 0;
            files_list();
            return;
        }
        i--;
    }
    const struct kits_entry *e = &files.entries[i];
    size_t length = strlen(files.path), name = strlen(e->name);
    if (length + 1 + name >= sizeof files.path) { show_message("Path too long"); return; }
    char path[FILES_PATH_BYTES];
    memcpy(path, files.path, length);
    path[length] = '/';
    memcpy(path + length + 1, e->name, name + 1);
    if (e->folder) {
        memcpy(files.path, path, sizeof path);
        files_list();
        return;
    }
    if (!kits_load(path, files.target, files.into_morph)) { show_message("Load failed"); return; }
    if (files.into_morph) snprintf(morph_page.kit, sizeof morph_page.kit, "%s", e->name);
    else if (files.target == KITS_ALL_CHANNELS) quick_follow(files.path, e->name);
    open_view(files.back);
    show_message("Loaded");
}

static void quick_walk(const char *folder, unsigned depth)
{
    static struct kits_entry levels[QUICK_DEPTH][FILES_MAX];
    struct kits_entry *entries = levels[depth];
    unsigned n = kits_list(folder, entries, FILES_MAX), here = QUICK_FOLDERS;
    for (unsigned i = 0; i < n; ++i) {
        char path[FILES_PATH_BYTES];
        if (snprintf(path, sizeof path, "%s/%s", folder, entries[i].name) >= (int)sizeof path) continue;
        if (entries[i].folder) {
            if (depth + 1 < QUICK_DEPTH) quick_walk(path, depth + 1);
            continue;
        }
        if (quick.count >= QUICK_KITS) return;
        if (here == QUICK_FOLDERS) {
            if (quick.folder_count >= QUICK_FOLDERS) return;
            here = quick.folder_count++;
            snprintf(quick.folders[here], FILES_PATH_BYTES, "%s", folder);
        }
        struct quick_kit *k = &quick.kits[quick.count++];
        memcpy(k->name, entries[i].name, KITS_NAME_BYTES);
        k->folder = (uint8_t)here;
    }
}

/* The current folder's run of kits in kits[] */
static void quick_folder_range(void)
{
    quick.first = quick.in_folder = 0;
    for (unsigned i = 0; i < quick.count; ++i) {
        if (strcmp(quick.folders[quick.kits[i].folder], quick.folder)) continue;
        if (!quick.in_folder) quick.first = i;
        quick.in_folder++;
    }
}

static void quick_list(void)
{
    quick.count = quick.folder_count = 0;
    if (kits_folder()) quick_walk(KITS_FOLDER, 0);
    quick.listed = 1;
    quick.cached = 0;
    quick_folder_range();
    if (quick.loaded < 0 && quick.in_folder) quick.index = quick.first;
}

/* The kit at index becomes the current one, and its folder kit CV's */
static void quick_set_loaded(unsigned index)
{
    const char *folder = quick.folders[quick.kits[index].folder];
    if (strcmp(quick.folder, folder)) {
        memcpy(quick.folder, folder, strlen(folder) + 1);  /* both FILES_PATH_BYTES */
        quick.cached = 0;
        settings_page.dirty = 1;    /* kept for kit CV */
        quick_folder_range();
    }
    quick.index = index;
    if (quick.loaded != (int)index) {
        quick.save_at = ui.now_ms + KIT_SAVE_DELAY_MS;
        quick.save_pending = 1;
    }
    quick.loaded = (int)index;
}

static int kit_cv_used(void)
{
    for (unsigned r = 0; r < KIT_CV_ROLES; ++r)
        if (kit_cv_input((enum kit_cv_role)r) != KIT_CV_OFF) return 1;
    return 0;
}

/* Kit CV: the folder's kits are read into memory once, so a change from CV
 * loads without the card */
static void kit_cv_tick(void)
{
    if (!kit_cv_used()) return;
    if (!quick.listed) quick_list();
    if (!quick.cached) {
        static struct kits_entry run[KITS_CACHE];
        unsigned n = quick.in_folder < KITS_CACHE ? quick.in_folder : KITS_CACHE;
        for (unsigned i = 0; i < n; ++i) {
            memcpy(run[i].name, quick.kits[quick.first + i].name, KITS_NAME_BYTES);
            run[i].folder = 0;
        }
        kits_cache(quick.folder, run, n);
        kit_cv_set_kits(n);
        quick.cached = 1;
    }
    int loaded = quick.loaded - (int)quick.first;
    int target = kit_cv_poll(loaded >= 0 && loaded < (int)quick.in_folder ? loaded : -1,
                             quick.in_folder < KITS_CACHE ? quick.in_folder : KITS_CACHE);
    if (target < 0) return;
    if (kits_load_cached((unsigned)target)) quick_set_loaded(quick.first + (unsigned)target);
}

/* A turn opens the kit overlay at the loaded kit and steps from there */
static void quick_turn(int steps)
{
    if (!quick.listed) quick_list();
    if (!quick.count) return;
    if (ui.edit == EDIT_NONE) {
        ui.edit = EDIT_VALUE;
        if (quick.loaded >= 0) quick.index = (unsigned)quick.loaded;
    }
    quick.index = (unsigned)clamp((int)quick.index + steps, 0, (int)quick.count - 1);
}

static void quick_load(void)
{
    if (!quick.listed || !quick.count || ui.edit == EDIT_NONE) {
        engine_audition(ui.channel, AUDITION_VELOCITY);
        return;
    }
    ui.edit = EDIT_NONE;
    char path[FILES_PATH_BYTES + KITS_NAME_BYTES + 1];
    snprintf(path, sizeof path, "%s/%s", quick.folders[quick.kits[quick.index].folder], quick.kits[quick.index].name);
    if (!kits_load(path, KITS_ALL_CHANNELS, 0)) { show_message("Load failed"); return; }
    quick_set_loaded(quick.index);
}

/* After a load from the browser, the quick browser follows its folder */
static void quick_follow(const char *folder, const char *name)
{
    if (!quick.listed) quick_list();
    quick.loaded = -1;
    for (unsigned i = 0; i < quick.count; ++i)
        if (!strcmp(quick.kits[i].name, name) && !strcmp(quick.folders[quick.kits[i].folder], folder)) {
            quick_set_loaded(i);
            return;
        }
}

const char *ui_kit_name(void) { return quick.loaded >= 0 ? quick.kits[quick.loaded].name : ""; }

void ui_kit_loaded(const char *folder, const char *name) { quick_follow(folder, name); }

void ui_kits_changed(void) { quick.listed = quick.cached = 0; }

/* Save: a name of 8 characters, as an LXR-02 kit's, the kind of file,
 * and Save. Pushing on Name steps through the characters; turning sets the
 * one marked; DATA 2 sweeps it. */
enum { SAVE_NAME, SAVE_KIND, SAVE_GO, SAVE_ROWS };
enum { SAVE_KIT, SAVE_SND, SAVE_KINDS };
static const char save_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
#define SAVE_CHARS          (sizeof save_charset - 1u)
static struct {
    char name[KITS_KIT_NAME_BYTES + 1];
    unsigned cursor, letter, kind;
    int blend, naming;
    enum view back;
} save_page = { .name = "        " };

static void open_view(enum view view);

static void open_save(int blend)
{
    save_page.back = ui.view;
    save_page.blend = blend;
    save_page.cursor = SAVE_GO;
    save_page.naming = 0;
    open_view(VIEW_SAVE);
}

static unsigned save_char_index(char c)
{
    const char *at = strchr(save_charset, c);
    return at && c ? (unsigned)(at - save_charset) : 0;
}

static void save_set_char(int index)
{
    save_page.name[save_page.letter] = save_charset[clamp(index, 0, SAVE_CHARS - 1)];
}

static void save_kit(void)
{
    char saved[32], text[40];
    int ok = save_page.kind == SAVE_SND ?
             kits_save_snd(ui.channel, save_page.name, save_page.blend, saved, sizeof saved) :
             kits_save(save_page.name, save_page.blend, saved, sizeof saved);
    if (!ok) { show_message("Save failed"); return; }
    quick.listed = quick.cached = 0;    /* the new kit joins the list */
    snprintf(text, sizeof text, "Saved %s", saved);
    open_view(save_page.back);
    show_message(text);
}

static void open_view(enum view view)
{
    utilities.confirming = 0;
    firmware_page.confirming = 0;
    if (view != ui.view && settings_page.dirty) {
        settings_page.dirty = 0;
        if (!settings_save()) show_message("Not saved: no card");
    }
    ui.view = view;
    ui.edit = EDIT_NONE;
    ui.cv_view = 0;
    ui.data2_taken = panel_data2();
    build_rows();
}

/* Mutate: moves every sound parameter of the channel's voice by up to
 * the amount, except its mix settings; opened from the channel overview or
 * the mixer, every channel's */
static uint32_t random_next(void)
{
    ui.random ^= ui.random << 13; ui.random ^= ui.random >> 17; ui.random ^= ui.random << 5;
    return ui.random;
}

static void mutate_channel(unsigned channel)
{
    int v = engine_voice(channel);
    if (v < 0) return;
    for (unsigned section = 0; section < SECTION_MIX; ++section) {
        const Page *page = &menuPages[v][section];
        const uint16_t *parameters = &page->bot1;
        for (unsigned k = 0; k < ROWS; ++k) {
            uint16_t p = parameters[k];
            if (p == PAR_NONE || p >= END_OF_SOUND_PARAMETERS || dtype(p) == DTYPE_TARGET_SELECTION_LFO ||
                dtype(p) == DTYPE_TARGET_SELECTION_VELO || dtype(p) == DTYPE_VOICE_LFO) continue;
            int low, high;
            value_range(p, &low, &high);
            int span = (high - low) * ui.mutate_amount / PERCENT;
            if (!span) continue;
            int step = (int)(random_next() % (2u * span + 1)) - span;
            engine_parameter(channel, p, (uint8_t)clamp(engine_parameter_value(channel, p) + step, low, high));
        }
    }
}

static void mutate(void)
{
    if (!ui.mutate_all) mutate_channel(ui.channel);
    else for (unsigned c = 0; c < CHANNELS; ++c) mutate_channel(c);
}

static void press_section(unsigned section)
{
    if (ui.view == VIEW_SECTION && ui.section == section) {
        ui.cv_view = !ui.cv_view;
        ui.edit = EDIT_NONE;
        return;
    }
    ui.section = section;
    open_view(VIEW_SECTION);
}

static void press_encoder(void)
{
    switch (ui.view) {
    case VIEW_CHANNELS: quick_load(); return;
    case VIEW_MUTATE: mutate(); return;
    case VIEW_INSTRUMENT:
        engine_set_voice(ui.channel, ui.cursor < ENGINE_TRIGGERS ? ui.cursor : ENGINE_NO_VOICE);
        return;
    case VIEW_FILES: files_push(); return;
    case VIEW_SAVE:
        if (save_page.cursor == SAVE_NAME) {
            if (!save_page.naming) { save_page.naming = 1; save_page.letter = 0; }
            else if (++save_page.letter == KITS_KIT_NAME_BYTES) save_page.naming = 0;
        } else if (save_page.cursor == SAVE_KIND) save_page.kind = (save_page.kind + 1u) % SAVE_KINDS;
        else save_kit();
        return;
    case VIEW_CHANNEL: ui.edit = ui.edit == EDIT_VALUE ? EDIT_NONE : EDIT_VALUE; return;
    case VIEW_MORPH:
        if (ui.cv_view) {
            ui.edit = ui.edit == EDIT_NONE ? EDIT_CV_SOURCE : ui.edit == EDIT_CV_SOURCE ? EDIT_CV_AMOUNT : EDIT_NONE;
            return;
        }
        switch (morph_page.cursor) {
        case MORPH_AMOUNT: ui.edit = ui.edit == EDIT_VALUE ? EDIT_NONE : EDIT_VALUE; return;
        case MORPH_KIT: open_files(1); return;
        case MORPH_CLEAR:
            for (unsigned c = 0; c < CHANNELS; ++c) engine_load_morph(c, NULL, 0);
            morph_page.kit[0] = 0;
            show_message("Morph cleared");
            return;
        default: open_save(1); return;
        }
    case VIEW_SETTINGS: ui.edit = ui.edit == EDIT_VALUE ? EDIT_NONE : EDIT_VALUE; return;
    case VIEW_UTILITIES:
        switch (utilities.cursor) {
        case UTILITY_MIDI: open_view(VIEW_SETTINGS); return;
        case UTILITY_DISPLAY: open_view(VIEW_DISPLAY); return;
        case UTILITY_AUDIO_IN: open_view(VIEW_AUDIO_IN); return;
        case UTILITY_KIT_CV: open_view(VIEW_KIT_CV); return;
        case UTILITY_WAVES: {
            char text[24];
            snprintf(text, sizeof text, "%u waves loaded", waves_load());
            show_message(text);
            return;
        }
        case UTILITY_MONITOR: open_view(VIEW_MONITOR); return;
        case UTILITY_ABOUT: open_view(VIEW_ABOUT); return;
        default: open_view(VIEW_FIRMWARE); return;
        }
    case VIEW_FIRMWARE: {
        int trial = remote_boot_mode() == BOOT_TRIAL;
        int available = firmware_page.cursor == FIRMWARE_STOCK ?
                        trial || install_stock_available() || install_previous(NULL, 0) : install_card_available();
        if (!available) { show_message(firmware_page.cursor == FIRMWARE_STOCK ? "No stock copy" : "No app on card"); return; }
        if (!firmware_page.confirming) { firmware_page.confirming = 1; return; }
        firmware_page.confirming = 0;
        int ok = firmware_page.cursor == FIRMWARE_CARD ? install_load_card() :
                 trial ? 1 : install_stock_available() ? install_stock() : install_restore();
        if (!ok) { show_message("Install failed"); return; }
        remote_reboot();
        show_message("Restarting");
        return;
    }
    case VIEW_DISPLAY: case VIEW_MONITOR: case VIEW_ABOUT: open_view(VIEW_UTILITIES); return;
    case VIEW_AUDIO_IN: case VIEW_KIT_CV: ui.edit = ui.edit == EDIT_VALUE ? EDIT_NONE : EDIT_VALUE; return;
    case VIEW_MIXER:
        if (ui.mix_field == MIX_MUTE) mix_set(ui.channel, MIX_MUTE, !mix_value(ui.channel, MIX_MUTE));
        else if (mix_value(ui.channel, ui.mix_field) >= 0) ui.edit = ui.edit == EDIT_VALUE ? EDIT_NONE : EDIT_VALUE;
        return;
    default: break;
    }
    if (!ui.row_count) return;
    if (!ui.cv_view) ui.edit = ui.edit == EDIT_VALUE ? EDIT_NONE : EDIT_VALUE;
    else ui.edit = ui.edit == EDIT_NONE ? EDIT_CV_SOURCE : ui.edit == EDIT_CV_SOURCE ? EDIT_CV_AMOUNT : EDIT_NONE;
}

/* A turn; pushed, it moves values in coarse steps */
static void turn(int steps, int pushed)
{
    if (ui.view == VIEW_MIXER) {
        if (pushed && ui.mix_field != MIX_MUTE && mix_value(ui.channel, ui.mix_field) >= 0) {
            if (ui.edit == EDIT_NONE) { ui.edit = EDIT_VALUE; ui.push_editing = 1; }
            int low, high;
            mix_range(ui.mix_field, &low, &high);
            steps *= coarse(low, high);
        }
        if (ui.edit == EDIT_VALUE) mix_set(ui.channel, ui.mix_field, mix_value(ui.channel, ui.mix_field) + steps);
        else if (!pushed) ui.mix_field = (unsigned)clamp((int)ui.mix_field + steps, 0, MIX_FIELDS - 1);
        return;
    }
    if (pushed && ui.row_count && (ui.view == VIEW_SECTION || ui.view == VIEW_FX)) {
        if (ui.edit == EDIT_NONE) { ui.edit = ui.cv_view ? EDIT_CV_AMOUNT : EDIT_VALUE; ui.push_editing = 1; }
        if (ui.edit == EDIT_VALUE) {
            int low, high;
            value_range(ui.rows[ui.cursor].parameter, &low, &high);
            steps *= coarse(low, high);
        } else if (ui.edit == EDIT_CV_AMOUNT) steps *= CV_COARSE_STEP;
    }
    if (ui.view == VIEW_CHANNELS) { quick_turn(steps); return; }
    if (ui.view == VIEW_MUTATE) { ui.mutate_amount = clamp(ui.mutate_amount + steps, 0, MUTATE_MAX); return; }
    if (ui.view == VIEW_INSTRUMENT) { ui.cursor = (unsigned)clamp((int)ui.cursor + steps, 0, ENGINE_TRIGGERS); return; }
    if (ui.view == VIEW_SAVE) {
        if (save_page.naming)
            save_set_char((int)save_char_index(save_page.name[save_page.letter]) + steps);
        else save_page.cursor = (unsigned)clamp((int)save_page.cursor + steps, 0, SAVE_ROWS - 1);
        return;
    }
    if (ui.view == VIEW_KIT_CV) {
        if (ui.edit == EDIT_VALUE || pushed) {
            if (ui.edit == EDIT_NONE) { ui.edit = EDIT_VALUE; ui.push_editing = 1; }
            kit_cv_step(steps * (pushed ? INPUTS_COLUMNS : 1));
            settings_page.dirty = 1;
        } else kit_cv_cursor = (unsigned)clamp((int)kit_cv_cursor + steps, 0, KIT_CV_ROLES - 1);
        return;
    }
    if (ui.view == VIEW_AUDIO_IN) {
        if (ui.edit == EDIT_VALUE || pushed) {
            if (ui.edit == EDIT_NONE) { ui.edit = EDIT_VALUE; ui.push_editing = 1; }
            int level = settings_input_level(audio_in_cursor) + steps * (pushed ? 8 : 1);
            settings_set_input_level(audio_in_cursor, (uint8_t)clamp(level, 0, SETTINGS_INPUT_MAX));
            settings_page.dirty = 1;
        } else audio_in_cursor = (unsigned)clamp((int)audio_in_cursor + steps, 0, 1);
        return;
    }
    if (ui.view == VIEW_CHANNEL) {
        if (ui.edit == EDIT_VALUE || pushed) {
            if (ui.edit == EDIT_NONE) { ui.edit = EDIT_VALUE; ui.push_editing = 1; }
            channel_step(channel_cursor, steps);
        } else channel_cursor = (unsigned)clamp((int)channel_cursor + steps, 0, CHANNEL_ROWS - 1);
        return;
    }
    if (ui.view == VIEW_MORPH) {
        if (ui.cv_view) {
            if (ui.edit == EDIT_CV_SOURCE) morph_route.source = (int8_t)clamp(morph_route.source + steps, 0, CV_SOURCES - 1);
            else if (ui.edit == EDIT_CV_AMOUNT || pushed) {
                if (ui.edit == EDIT_NONE) { ui.edit = EDIT_CV_AMOUNT; ui.push_editing = 1; }
                morph_route.amount = (int8_t)clamp(morph_route.amount + steps * (pushed ? CV_COARSE_STEP : 1),
                                                   -CV_AMOUNT_MAX, CV_AMOUNT_MAX);
            }
            if (morph_route.source && !morph_route.amount) morph_route.amount = CV_AMOUNT_DEFAULT;
            cv_routes_changed = 1;
            return;
        }
        if (morph_page.cursor == MORPH_AMOUNT && (ui.edit == EDIT_VALUE || pushed)) {
            if (ui.edit == EDIT_NONE) { ui.edit = EDIT_VALUE; ui.push_editing = 1; }
            engine_morph((uint8_t)clamp(engine_morph_value() + steps * (pushed ? MORPH_COARSE_STEP : 1), 0, ENGINE_MORPH_MAX));
        } else if (!pushed) morph_page.cursor = (unsigned)clamp((int)morph_page.cursor + steps, 0, MORPH_ROWS - 1);
        return;
    }
    if (ui.view == VIEW_FIRMWARE) {
        firmware_page.confirming = 0;
        firmware_page.cursor = (unsigned)clamp((int)firmware_page.cursor + steps, 0, FIRMWARE_ROWS - 1);
        return;
    }
    if (ui.view == VIEW_UTILITIES) {
        utilities.confirming = 0;
        utilities.cursor = (unsigned)clamp((int)utilities.cursor + steps, 0, UTILITIES - 1);
        return;
    }
    if (ui.view == VIEW_DISPLAY) {
        settings_set_brightness((unsigned)clamp((int)settings_brightness() + steps, 1, DISPLAY_BRIGHTNESS_MAX));
        settings_page.dirty = 1;
        return;
    }
    if (ui.view == VIEW_MONITOR || ui.view == VIEW_ABOUT) return;
    if (ui.view == VIEW_SETTINGS) {
        if (ui.edit == EDIT_VALUE || pushed) {
            if (pushed && ui.edit == EDIT_NONE) { ui.edit = EDIT_VALUE; ui.push_editing = 1; }
            setting_step(settings_page.cursor, pushed ? steps * (setting_is_note(settings_page.cursor) ? 12 : 4) : steps);
        } else settings_page.cursor = (unsigned)clamp((int)settings_page.cursor + steps, 0, SETTINGS_ROWS - 1);
        return;
    }
    if (ui.view == VIEW_FILES) {
        if (files_rows()) files.cursor = (unsigned)clamp((int)files.cursor + steps, 0, (int)files_rows() - 1);
        return;
    }
    if (!ui.row_count) return;
    struct row *r = &ui.rows[ui.cursor];
    struct cv_route *cv = route_of(r->parameter);
    switch (ui.edit) {
    case EDIT_NONE: ui.cursor = (unsigned)clamp((int)ui.cursor + steps, 0, (int)ui.row_count - 1); break;
    case EDIT_VALUE:
        set_value(r->parameter, engine_parameter_value(ui.channel, r->parameter) + steps);
        if (r->parameter == PAR_FX_TYPE) ui.edit = EDIT_VALUE;
        break;
    case EDIT_CV_SOURCE:
        cv->source = (int8_t)clamp(cv->source + steps, 0, CV_SOURCES - 1);
        if (cv->source && !cv->amount) cv->amount = CV_AMOUNT_DEFAULT;
        cv_routes_changed = 1;
        break;
    case EDIT_CV_AMOUNT:
        cv->amount = (int8_t)clamp(cv->amount + steps, -CV_AMOUNT_MAX, CV_AMOUNT_MAX);
        cv_routes_changed = 1;
        break;
    }
}

void ui_init(void)
{
    ui.mutate_amount = MUTATE_DEFAULT;
    ui.random = MUTATE_DEFAULT | 1u;
    open_view(VIEW_CHANNELS);
}

void ui_event(struct panel_event e)
{
    if (!ui.splash_done) {
        ui.splash_done = 1;
        return;
    }
    if (e.kind == EVENT_TURN) {
        int pushed = panel_button_down(BUTTON_ENCODER);
        if (pushed) ui.push_turned = 1;
        turn(e.steps, pushed);
        return;
    }
    /* The push acts on release, unless the encoder turned while it was down */
    if (e.button == BUTTON_ENCODER) {
        if (e.kind == EVENT_PRESS) ui.push_turned = 0;
        else if (e.kind == EVENT_RELEASE) {
            if (ui.push_editing) { ui.edit = EDIT_NONE; ui.push_editing = 0; }
            else if (!ui.push_turned) press_encoder();
        }
        return;
    }
    /* Letting go of a channel button ends its note, as a gate falling */
    if (e.kind == EVENT_RELEASE && e.button <= BUTTON_CHANNEL_8) { engine_release(e.button - BUTTON_CHANNEL_1); return; }
    if (e.kind != EVENT_PRESS) return;
    if (e.button <= BUTTON_CHANNEL_8) {
        ui.channel = e.button - BUTTON_CHANNEL_1;
        engine_select(ui.channel);
        engine_audition(ui.channel, AUDITION_VELOCITY);
        ui.edit = EDIT_NONE;
        if (ui.view == VIEW_INSTRUMENT) ui.cursor = voice() < 0 ? ENGINE_TRIGGERS : (unsigned)voice();
        else build_rows();
        return;
    }
    for (unsigned i = 0; i < sizeof section_buttons / sizeof section_buttons[0]; ++i)
        if (section_buttons[i].button == e.button) { press_section(section_buttons[i].section); return; }
    switch (e.button) {
    case BUTTON_CHANNELS: open_view(VIEW_CHANNELS); break;
    case BUTTON_PLAY_MODE: {    /* as the Assimil8or's: the selected channel between 1 Shot and Gated */
        int gated = !engine_mix_value(ui.channel, ENGINE_MIX_GATED);
        engine_mix(ui.channel, ENGINE_MIX_GATED, (uint8_t)gated);
        show_message(gated ? "Gated" : "1 Shot");
        break;
    }
    case BUTTON_PAN_MIX: open_view(ui.view == VIEW_MIXER ? VIEW_FX : VIEW_MIXER); break;
    case BUTTON_MUTATE:
        ui.mutate_all = ui.view == VIEW_CHANNELS || ui.view == VIEW_MIXER;
        open_view(VIEW_MUTATE);
        break;
    case BUTTON_LOAD: if (ui.view == VIEW_FILES) open_view(files.back); else open_files(0); break;
    case BUTTON_SAVE: if (ui.view == VIEW_SAVE) open_view(save_page.back); else open_save(0); break;
    case BUTTON_SAMPLING_SETUP: open_view(VIEW_CHANNEL); break;
    case BUTTON_ZONES:
        if (ui.view == VIEW_MORPH) { ui.cv_view = !ui.cv_view; ui.edit = EDIT_NONE; }
        else open_view(VIEW_MORPH);
        break;
    case BUTTON_UTILITIES: open_view(VIEW_UTILITIES); break;
    case BUTTON_SAMPLE:
        open_view(VIEW_INSTRUMENT);
        ui.cursor = voice() < 0 ? ENGINE_TRIGGERS : (unsigned)voice();
        break;
    default: break;
    }
}

static void poll_data2(void)
{
    float knob = panel_data2(), moved = knob - ui.data2_taken;
    if (moved < DATA2_THRESHOLD && moved > -DATA2_THRESHOLD) return;
    ui.data2_taken = knob;
    if (ui.view == VIEW_SAVE && save_page.naming) {
        save_set_char((int)(knob * (SAVE_CHARS - 1) + 0.5f));
        return;
    }
    if (ui.view == VIEW_AUDIO_IN) {
        settings_set_input_level(audio_in_cursor, (uint8_t)(knob * SETTINGS_INPUT_MAX + 0.5f));
        settings_page.dirty = 1;
        return;
    }
    if (ui.view == VIEW_MORPH && !ui.cv_view) {
        engine_morph((uint8_t)(knob * ENGINE_MORPH_MAX + 0.5f));
        return;
    }
    if (ui.view == VIEW_MIXER && ui.mix_field != MIX_MUTE) {
        int low, high;
        mix_range(ui.mix_field, &low, &high);
        mix_set(ui.channel, ui.mix_field, low + (int)(knob * (high - low) + 0.5f));
        return;
    }
    if ((ui.view != VIEW_SECTION && ui.view != VIEW_FX) || ui.cv_view || !ui.row_count) return;
    int low, high;
    uint16_t p = ui.rows[ui.cursor].parameter;
    value_range(p, &low, &high);
    set_value(p, low + (int)(knob * (high - low) + 0.5f));
}

void ui_tick(uint32_t now_ms)
{
    ui.now_ms = now_ms;
    kit_cv_tick();
    if (quick.save_pending && (int32_t)(now_ms - quick.save_at) >= 0) {
        quick.save_pending = 0;
        settings_save();
    }
    poll_data2();
    if (cv_routes_changed) publish_cv_routes();
    if (ui.view == VIEW_FX) build_rows();     /* the FX type sets the rows */
    /* As the Assimil8or's: full while held, dim while the voice still sounds */
    for (unsigned c = 0; c < CHANNELS; ++c) {
        if (engine_held(c)) panel_led(LED_CHANNEL_1 + c, 1);
        else if (engine_sounding(c)) panel_led_dim(LED_CHANNEL_1 + c);
        else panel_led(LED_CHANNEL_1 + c, 0);
    }
    /* The Play Mode LEDs show the selected channel's, as the Assimil8or's */
    int gated = engine_mix_value(ui.channel, ENGINE_MIX_GATED) != 0;
    panel_led(LED_PLAY_MODE_GATED, gated);
    panel_led(LED_PLAY_MODE_1_SHOT, !gated);
}

/* Drawing */
static const char *const section_tags[SECTIONS] = {
    "OSC", "AMP", "MOD", "FM", "TRANS", "FILTER", "LFO", "MIX",
};

static void upper(char *s) { for (; *s; ++s) if (*s >= 'a' && *s <= 'z') *s -= 'a' - 'A'; }

/* A filled badge with a number, then a title, both in the large font */
/* Text wrapped at spaces to the screen's right edge; returns the next line's y */
#define WRAP_LINE_GAP       1
static int wrapped_text(const struct font *f, int x, int y, const char *text, uint16_t colour)
{
    int columns = (VALUE_RIGHT + 1 - x) / f->width;
    while (*text) {
        int n = (int)strlen(text);
        if (n > columns) {
            n = columns;
            while (n > 0 && text[n] != ' ') --n;
            if (n == 0) n = columns;
        }
        char line[DISPLAY_SIDE];
        memcpy(line, text, (size_t)n);
        line[n] = 0;
        screen_text(f, x, y, line, colour);
        y += f->height + WRAP_LINE_GAP;
        text += n;
        while (*text == ' ') ++text;
    }
    return y;
}

static void header(int number, const char *title, const char *tag, int tag_amber)
{
    char text[24];
    if (number > 0) {
        screen_fill(0, 0, BADGE_SIZE, BADGE_SIZE, ACCENT);
        snprintf(text, sizeof text, "%d", number);
        screen_text(&font_8_16, (BADGE_SIZE - font_8_16.width) / 2, 0, text, BLACK);
    }
    snprintf(text, sizeof text, "%s", title);
    upper(text);
    screen_text(&font_8_16, number > 0 ? BADGE_SIZE + 4 : 0, 0, text, WHITE);
    snprintf(text, sizeof text, "%s", tag);
    screen_text(&font_5_8, VALUE_RIGHT - screen_text_width(&font_5_8, text) + 1, 0, text,
                tag_amber ? AMBER : GREY);
    screen_fill(0, HEADER_HEIGHT - 1, DISPLAY_SIDE, 1, RULE);
}

/* The eight sections as dots under the tag, the open one lit */
static void section_dots(void)
{
    int x = VALUE_RIGHT + 1 - SECTIONS * DOT_PITCH + (DOT_PITCH - DOT_SIZE);
    for (unsigned s = 0; s < SECTIONS; ++s)
        screen_fill(x + s * DOT_PITCH, 10, DOT_SIZE, DOT_SIZE, s == ui.section ? ACCENT : OFF);
}

static void segment_bar(int x, int y, int segments, int lit, uint16_t on, uint16_t off)
{
    for (int i = 0; i < segments; ++i)
        screen_fill(x + i * SEGMENT_PITCH, y, SEGMENT_WIDTH, BAR_HEIGHT, i < lit ? on : off);
}

static void activity_footer(void)
{
    char n[2] = { 0 };
    for (unsigned c = 0; c < CHANNELS; ++c) {
        int x = c * CELL_PITCH, t = engine_voice(c);
        int lit = engine_held(c) || engine_sounding(c);
        uint16_t fill = t < 0 ? BLACK : lit ? ACCENT : c == ui.channel ? GREY : OFF;
        screen_fill(x, FOOTER_TOP, CELL_WIDTH, DISPLAY_SIDE - FOOTER_TOP, fill);
        n[0] = (char)('1' + c);
        screen_text(&font_5_8, x + (CELL_WIDTH - font_5_8.width) / 2, FOOTER_TOP + 2, n,
                    t < 0 ? OFF : lit || c == ui.channel ? BLACK : GREY);
    }
}

static void draw_value_row(unsigned i, int y)
{
    const struct row *r = &ui.rows[i];
    int selected = i == ui.cursor;
    int text_y = y + (ROW_HEIGHT - font_6_12.height) / 2;
    if (selected) {
        screen_fill(0, y, DISPLAY_SIDE, ROW_HEIGHT, SELECTED);
        screen_fill(0, y, ACCENT_WIDTH, ROW_HEIGHT, ui.cv_view ? AMBER : ACCENT);
    }
    screen_text(&font_6_12, INSET, text_y, row_name(r), selected ? WHITE : LABEL);

    char value[16];
    uint8_t raw = engine_parameter_value(ui.channel, r->parameter);
    const struct cv_route *cv = route_of(r->parameter);
    uint16_t colour = WHITE;
    int editing = selected && ui.edit != EDIT_NONE;
    if (!ui.cv_view) {
        row_value_text(r, raw, value, sizeof value);
        if (cv->source) {   /* a routed parameter shows an amber tick */
            screen_fill(INSET - 4, text_y + 4, 2, 2, AMBER);
        }
    } else {
        char source[4];
        cv_source_text(cv->source, source, sizeof source);
        if (cv->source) snprintf(value, sizeof value, "%s %+d", source, cv->amount);
        else snprintf(value, sizeof value, "--");
        colour = cv->source ? AMBER : GREY;
    }
    int x = VALUE_RIGHT - screen_text_width(&font_6_12, value);
    if (editing) {
        uint16_t pill = ui.cv_view ? AMBER : ACCENT;
        screen_fill(x - 3, y, VALUE_RIGHT - x + 4, ROW_HEIGHT, pill);
        screen_text(&font_6_12, x, text_y, value, BLACK);
    } else screen_text(&font_6_12, x, text_y, value, colour);
}

/* The selected parameter's position as a segmented bar under the list */
static void focus_bar(void)
{
    if (!ui.row_count || ui.cv_view) return;
    uint16_t p = ui.rows[ui.cursor].parameter;
    int low, high;
    value_range(p, &low, &high);
    int raw = engine_parameter_value(ui.channel, p);
    int segments = (DISPLAY_SIDE - 2 * INSET) / SEGMENT_PITCH;
    int lit = high > low ? ((raw - low) * segments + (high - low) / 2) / (high - low) : 0;
    segment_bar(INSET, FOCUS_BAR_TOP, segments, lit,
                ACCENT, OFF);
}

static int row_value(uint8_t name, int fallback)
{
    for (unsigned i = 0; i < ui.row_count; ++i)
        if (ui.rows[i].name == name) return engine_parameter_value(ui.channel, ui.rows[i].parameter);
    return fallback;
}

/* A picture of the section in the space under its rows */
static void draw_graph(void)
{
    int top = ROW_TOP + (int)ui.row_count * ROW_HEIGHT + GRAPH_GAP;
    int height = FOCUS_BAR_TOP - GRAPH_GAP - top, width = VALUE_RIGHT - INSET;
    if (height < GRAPH_MIN_HEIGHT) return;
    switch (ui.section) {
    case 0:
        graph_wave(row_value(TEXT_WAVEFORM, 0), row_value(TEXT_PWM, 0), INSET, top, width, height);
        break;
    case 1:
        graph_envelope(row_value(TEXT_ATTACK, 0), row_value(TEXT_DECAY, row_value(TEXT_DECAY_CLOSED, 0)),
                       row_value(TEXT_EG_SLOPE, 0), INSET, top, width, height);
        break;
    case 5:
        graph_filter(row_value(TEXT_FILTER_TYPE, 0) + 1, row_value(TEXT_FILTER_F, 0),
                     row_value(TEXT_FILTER_RESO, 0), INSET, top, width, height);
        break;
    default:
        break;
    }
}

/* Editing: the parameter's name, its value large and a full-width bar, in
 * the half of the list away from the selected row */
/* The edit overlay: a framed box with the name, a note, the value large
 * and a bar for its position in low-high */
static void overlay_box(int y, const char *title, const char *what, const char *value,
                        int low, int high, int position, uint16_t accent)
{
    int x = OVERLAY_MARGIN, w = DISPLAY_SIDE - 2 * OVERLAY_MARGIN;
    screen_fill(x - 1, y - 1, w + 2, OVERLAY_HEIGHT + 2, BLACK);
    screen_fill(x, y, w, OVERLAY_HEIGHT, accent);
    screen_fill(x + 1, y + 1, w - 2, OVERLAY_HEIGHT - 2, PANEL);
    screen_text(&font_6_12, x + OVERLAY_PAD, y + OVERLAY_PAD - 1, title, LABEL);
    screen_text(&font_5_8, x + w - OVERLAY_PAD - screen_text_width(&font_5_8, what), y + OVERLAY_PAD, what, accent);
    const struct font *big = screen_text_width(&font_12_24, value) <= w - 2 * OVERLAY_PAD ? &font_12_24 : &font_8_16;
    screen_text(big, x + w - OVERLAY_PAD - screen_text_width(big, value), y + 16, value, WHITE);
    int segments = (w - 2 * OVERLAY_PAD) / SEGMENT_PITCH;
    int lit = high > low ? ((position - low) * segments + (high - low) / 2) / (high - low) : 0;
    segment_bar(x + OVERLAY_PAD, y + OVERLAY_HEIGHT - OVERLAY_PAD - BAR_HEIGHT, segments, lit, accent, OFF);
}

static void draw_overlay(void)
{
    if (ui.edit == EDIT_NONE || !ui.row_count) return;
    const struct row *r = &ui.rows[ui.cursor];
    int row_y = ROW_TOP + (int)ui.cursor * ROW_HEIGHT;
    int lower = row_y < (ROW_TOP + FOCUS_BAR_TOP) / 2;
    int y = lower ? FOCUS_BAR_TOP - OVERLAY_HEIGHT - OVERLAY_MARGIN : ROW_TOP + OVERLAY_MARGIN;

    char value[16], what[16];
    uint8_t raw = engine_parameter_value(ui.channel, r->parameter);
    const struct cv_route *cv = route_of(r->parameter);
    int low, high, position;
    if (!ui.cv_view) {
        row_value_text(r, raw, value, sizeof value);
        what[0] = 0;
        value_range(r->parameter, &low, &high);
        position = raw;
    } else if (ui.edit == EDIT_CV_SOURCE) {
        cv_source_text(cv->source, value, sizeof value);
        if (cv->source) volts_text(inputs_cv[cv_input(route_channel(ui.channel, r->parameter), cv->source)],
                                   what, sizeof what);
        else snprintf(what, sizeof what, "CV input");
        low = 0; high = CV_SOURCES - 1; position = cv->source;
    } else {
        snprintf(value, sizeof value, "%+d", cv->amount);
        snprintf(what, sizeof what, "CV amount");
        low = -CV_AMOUNT_MAX; high = CV_AMOUNT_MAX; position = cv->amount;
    }
    overlay_box(y, row_name(r), what, value, low, high, position, ui.cv_view ? AMBER : ACCENT);
}

/* Mixer: a strip per channel with its level fader and meter, pan, FX
 * send and mute. The selected field is drawn bright in every strip. */
#define STRIP_TOP           (ROW_TOP + 2)
#define FADER_HEIGHT        52
#define FADER_X             3
#define FADER_WIDTH         6
#define METER_X             10
#define METER_WIDTH         2
#define PAN_Y               (STRIP_TOP + FADER_HEIGHT + 6)
#define SEND_Y              (PAN_Y + 9)
#define MUTE_Y              (SEND_Y + 8)
#define MUTE_HEIGHT         10
#define STRIP_INNER         13
#define PAN_DOT             3

static void draw_strip(unsigned c)
{
    int x = (int)c * CELL_PITCH, selected = c == ui.channel;
    if (selected) screen_fill(x, ROW_TOP, CELL_WIDTH, FOOTER_TOP - ROW_TOP - 1, SELECTED);
    if (mix_value(c, MIX_LEVEL) < 0) {
        screen_text(&font_5_8, x + (CELL_WIDTH - 2 * font_5_8.width) / 2, STRIP_TOP + FADER_HEIGHT / 2, "--", OFF);
        return;
    }
    int muted = mix_value(c, MIX_MUTE);
    int low, high;
    /* Level: a fader filled to the level, with the channel's meter beside it */
    mix_range(MIX_LEVEL, &low, &high);
    int fill = (mix_value(c, MIX_LEVEL) - low) * FADER_HEIGHT / (high - low);
    uint16_t level_colour = muted ? GREY : ui.mix_field == MIX_LEVEL ? ACCENT : LABEL;
    screen_fill(x + FADER_X, STRIP_TOP, FADER_WIDTH, FADER_HEIGHT - fill, OFF);
    screen_fill(x + FADER_X, STRIP_TOP + FADER_HEIGHT - fill, FADER_WIDTH, fill, level_colour);
    int meter = (int)(meters_peak[c] * METER_GAIN * FADER_HEIGHT);
    meter = meter > FADER_HEIGHT ? FADER_HEIGHT : meter;
    screen_fill(x + METER_X, STRIP_TOP + FADER_HEIGHT - meter, METER_WIDTH, meter, WHITE);
    /* Pan: a bar from the centre mark towards the side */
    int centre = x + 1 + STRIP_INNER / 2;
    int reach = (mix_value(c, MIX_PAN) - BIPOLAR_CENTRE) * (STRIP_INNER / 2) / BIPOLAR_CENTRE;
    reach = clamp(reach, -(int)(STRIP_INNER / 2), STRIP_INNER / 2);
    screen_fill(x + 1, PAN_Y, STRIP_INNER, PAN_DOT, OFF);
    if (reach) screen_fill(reach < 0 ? centre + reach : centre + 1, PAN_Y, reach < 0 ? -reach : reach, PAN_DOT,
                           ui.mix_field == MIX_PAN ? ACCENT : LABEL);
    screen_fill(centre, PAN_Y - 1, 1, PAN_DOT + 2, ui.mix_field == MIX_PAN ? WHITE : GREY);
    /* FX send: a short bar */
    int send = mix_value(c, MIX_SEND) * STRIP_INNER / (int)ENGINE_SEND_MAX;
    screen_fill(x + 1, SEND_Y, STRIP_INNER, BAR_HEIGHT, OFF);
    screen_fill(x + 1, SEND_Y, send, BAR_HEIGHT, ui.mix_field == MIX_SEND ? ACCENT : LABEL);
    /* Mute: a lit M */
    uint16_t box = muted ? ACCENT : ui.mix_field == MIX_MUTE ? GREY : OFF;
    screen_fill(x + 1, MUTE_Y, STRIP_INNER, MUTE_HEIGHT, box);
    screen_text(&font_5_8, x + 1 + (STRIP_INNER - font_5_8.width) / 2, MUTE_Y + 1, "M",
                muted ? BLACK : ui.mix_field == MIX_MUTE ? WHITE : GREY);
}

static void draw_mixer(void)
{
    char value[16];
    mix_text(ui.channel, ui.mix_field, value, sizeof value);
    header((int)ui.channel + 1, mix_names[ui.mix_field], value, 0);
    for (unsigned c = 0; c < CHANNELS; ++c) draw_strip(c);
    if (ui.edit != EDIT_VALUE) return;
    int low, high;
    mix_range(ui.mix_field, &low, &high);
    overlay_box(ROW_TOP + OVERLAY_MARGIN, mix_names[ui.mix_field], "", value, low, high,
                mix_value(ui.channel, ui.mix_field), ACCENT);
}

static void draw_page(void)
{
    int v = voice();
    if (ui.view == VIEW_FX) header(0, "FX bus", ui.cv_view ? "CV" : "", ui.cv_view);
    else {
        header((int)ui.channel + 1, v >= 0 ? voice_names[v] : "Empty",
               ui.cv_view ? "CV" : section_tags[ui.section], ui.cv_view);
        section_dots();
    }
    if (!ui.row_count) wrapped_text(&font_6_12, INSET, ROW_TOP, "This voice has no settings in this section.", LABEL);
    for (unsigned i = 0; i < ui.row_count; ++i) draw_value_row(i, ROW_TOP + i * ROW_HEIGHT);
    if (ui.view == VIEW_SECTION && !ui.cv_view) draw_graph();
    focus_bar();
    draw_overlay();
}

/* A kit's name without its extension, its folder's name and its place there */
static void kit_text(unsigned index, char *name, char *where, char *count, unsigned *place, unsigned *total)
{
    snprintf(name, KITS_NAME_BYTES, "%s", quick.kits[index].name);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    upper(name);
    const struct quick_kit *k = &quick.kits[index];
    unsigned first = index;
    while (first > 0 && quick.kits[first - 1].folder == k->folder) first--;
    *total = 0;
    for (unsigned i = first; i < quick.count && quick.kits[i].folder == k->folder; ++i) ++*total;
    *place = index - first + 1;
    const char *folder = strrchr(quick.folders[k->folder], '/');
    snprintf(where, 12, "%.8s", folder && strcmp(quick.folders[k->folder], KITS_FOLDER) ? folder + 1 : "");
    snprintf(count, 12, "%u/%u", *place, *total);
}

static void draw_channels(void)
{
    /* The loaded kit, its folder and its place in it; none until a load */
    char name[KITS_NAME_BYTES], where[12], count[12];
    unsigned place, total;
    if (quick.loaded >= 0) {
        kit_text((unsigned)quick.loaded, name, where, count, &place, &total);
        screen_text(&font_8_16, 0, 0, name, WHITE);
        screen_text(&font_5_8, VALUE_RIGHT - screen_text_width(&font_5_8, where) + 1, 0, where, GREY);
        screen_text(&font_5_8, VALUE_RIGHT - screen_text_width(&font_5_8, count) + 1, 9, count, GREY);
        screen_fill(0, HEADER_HEIGHT - 1, DISPLAY_SIDE, 1, RULE);
    } else header(0, "Channels", "", 0);
    for (unsigned c = 0; c < CHANNELS; ++c) {
        int y = ROW_TOP + c * ROW_HEIGHT, t = engine_voice(c);
        int selected = c == ui.channel, text_y = y + (ROW_HEIGHT - font_6_12.height) / 2;
        if (selected) {
            screen_fill(0, y, DISPLAY_SIDE, ROW_HEIGHT, SELECTED);
            screen_fill(0, y, ACCENT_WIDTH, ROW_HEIGHT, ACCENT);
        }
        char line[20];
        snprintf(line, sizeof line, "%u %s", c + 1, t >= 0 ? voice_names[t] : "--");
        screen_text(&font_6_12, INSET, text_y, line, t < 0 ? GREY : selected ? WHITE : LABEL);
        if (t < 0) continue;
        int lit = (int)(meters_peak[c] * METER_GAIN * METER_SEGMENTS);
        segment_bar(VALUE_RIGHT - METER_SEGMENTS * SEGMENT_PITCH + 1, y + (ROW_HEIGHT - BAR_HEIGHT) / 2,
                    METER_SEGMENTS, lit, ACCENT, OFF);
    }
    /* Browsing: the kit a push loads */
    if (ui.edit == EDIT_NONE || !quick.listed || !quick.count) return;
    kit_text(quick.index, name, where, count, &place, &total);
    char what[24];
    snprintf(what, sizeof what, "%s%s%s", where, where[0] ? " " : "", count);
    overlay_box(ROW_TOP + OVERLAY_MARGIN, "Kit", what, name, 1, (int)total, (int)place, ACCENT);
}

static void draw_mutate(void)
{
    int v = voice();
    if (ui.mutate_all) header(0, "Mutate", "ALL", 0);
    else header((int)ui.channel + 1, v >= 0 ? voice_names[v] : "Empty", "MUTATE", 0);
    char amount[8];
    snprintf(amount, sizeof amount, "%d%%", ui.mutate_amount);
    screen_text(&font_6_12, INSET, ROW_TOP + 4, "Amount", LABEL);
    screen_text(&font_12_24, VALUE_RIGHT - screen_text_width(&font_12_24, amount), ROW_TOP + 18, amount, WHITE);
    int segments = (DISPLAY_SIDE - 2 * INSET) / SEGMENT_PITCH;
    segment_bar(INSET, ROW_TOP + 48, segments, segments * ui.mutate_amount / MUTATE_MAX, ACCENT, OFF);
    screen_text(&font_6_12, INSET, ROW_TOP + 66, "Push to mutate", GREY);
}

/* The voice types; the channel's own is marked, push assigns the selected */
static void draw_instrument(void)
{
    int v = voice();
    header((int)ui.channel + 1, v >= 0 ? voice_names[v] : "Empty", "VOICE", 0);
    for (unsigned i = 0; i <= ENGINE_TRIGGERS; ++i) {
        int y = ROW_TOP + (int)i * ROW_HEIGHT, text_y = y + (ROW_HEIGHT - font_6_12.height) / 2;
        int selected = i == ui.cursor, current = (int)i == (v < 0 ? (int)ENGINE_TRIGGERS : v);
        if (selected) {
            screen_fill(0, y, DISPLAY_SIDE, ROW_HEIGHT, SELECTED);
            screen_fill(0, y, ACCENT_WIDTH, ROW_HEIGHT, ACCENT);
        }
        screen_text(&font_6_12, INSET, text_y, i < ENGINE_TRIGGERS ? voice_names[i] : "None",
                    selected ? WHITE : LABEL);
        if (current) screen_text(&font_6_12, VALUE_RIGHT - font_6_12.width, text_y, "*", ACCENT);
    }
}

/* The Load browser: a scrolling list, folders in grey with a slash */
static void draw_files(void)
{
    const char *folder = strrchr(files.path, '/');
    char target[16];
    if (files.target == KITS_ALL_CHANNELS) snprintf(target, sizeof target, files.into_morph ? "MORPH ALL" : "ALL");
    else snprintf(target, sizeof target, files.into_morph ? "MORPH CH %d" : "CH %d", files.target + 1);
    header(0, folder ? folder + 1 : files.path, target, 0);
    unsigned rows = files_rows();
    if (!rows) { screen_text(&font_6_12, INSET, ROW_TOP, "No kits here", GREY); return; }
    unsigned top = files.cursor >= ROWS ? files.cursor - ROWS + 1 : 0;
    for (unsigned r = top; r < rows && r < top + ROWS; ++r) {
        int y = ROW_TOP + (int)(r - top) * ROW_HEIGHT, text_y = y + (ROW_HEIGHT - font_6_12.height) / 2;
        int selected = r == files.cursor;
        if (selected) {
            screen_fill(0, y, DISPLAY_SIDE, ROW_HEIGHT, SELECTED);
            screen_fill(0, y, ACCENT_WIDTH, ROW_HEIGHT, ACCENT);
        }
        char line[KITS_NAME_BYTES + 2];
        int up = files_nested() && r == 0;
        const struct kits_entry *e = &files.entries[r - (files_nested() ? 1u : 0u)];
        if (up) snprintf(line, sizeof line, "..");
        else snprintf(line, sizeof line, "%s%s", e->name, e->folder ? "/" : "");
        screen_text(&font_6_12, INSET, text_y, line, up || e->folder ? LABEL : selected ? WHITE : LABEL);
    }
}

static void draw_settings(void)
{
    header(0, "MIDI", "", 0);
    unsigned top = settings_page.cursor >= ROWS ? settings_page.cursor - ROWS + 1 : 0;
    for (unsigned r = top; r < SETTINGS_ROWS && r < top + ROWS; ++r) {
        int y = ROW_TOP + (int)(r - top) * ROW_HEIGHT, text_y = y + (ROW_HEIGHT - font_6_12.height) / 2;
        int selected = r == settings_page.cursor, editing = selected && ui.edit == EDIT_VALUE;
        if (selected) {
            screen_fill(0, y, DISPLAY_SIDE, ROW_HEIGHT, SELECTED);
            screen_fill(0, y, ACCENT_WIDTH, ROW_HEIGHT, ACCENT);
        }
        char name[24], value[8];
        setting_text(r, name, sizeof name, value, sizeof value);
        screen_text(&font_6_12, INSET, text_y, name, selected ? WHITE : LABEL);
        screen_text(&font_6_12, VALUE_RIGHT - screen_text_width(&font_6_12, value), text_y, value,
                    editing ? ACCENT : selected ? WHITE : LABEL);
    }
}

static void list_row(unsigned i, const char *text, int selected, const char *value, uint16_t value_colour)
{
    int y = ROW_TOP + (int)i * ROW_HEIGHT, text_y = y + (ROW_HEIGHT - font_6_12.height) / 2;
    if (selected) {
        screen_fill(0, y, DISPLAY_SIDE, ROW_HEIGHT, SELECTED);
        screen_fill(0, y, ACCENT_WIDTH, ROW_HEIGHT, ACCENT);
    }
    screen_text(&font_6_12, INSET, text_y, text, selected ? WHITE : LABEL);
    if (value) screen_text(&font_6_12, VALUE_RIGHT - screen_text_width(&font_6_12, value), text_y, value, value_colour);
}

static void draw_firmware(void)
{
    static const char *const names[FIRMWARE_ROWS] = { "Stock firmware", "Load from card" };
    int trial = remote_boot_mode() == BOOT_TRIAL;
    header(0, "Firmware", trial ? "TRIAL" : "", 0);
    for (unsigned i = 0; i < FIRMWARE_ROWS; ++i) {
        int selected = i == firmware_page.cursor;
        list_row(i, names[i], selected, selected && firmware_page.confirming ? "Push" : NULL, ACCENT);
    }
    const char *note;
    char previous[24];
    if (firmware_page.confirming) note = "Push again: install, restart";
    else if (firmware_page.cursor == FIRMWARE_CARD) note = install_card_available() ? "Installs app from the card" : "No app file on the card";
    else if (trial) note = "Restarts the installed one";
    else if (install_stock_available()) note = "Kept on the module";
    else note = install_previous(previous, sizeof previous) ? "Swaps in the previous one" : "No stock copy";
    wrapped_text(&font_5_8, INSET, ROW_TOP + FIRMWARE_ROWS * ROW_HEIGHT + 4, note, GREY);
}

static void draw_utilities(void)
{
    header(0, "Utilities", "", 0);
    for (unsigned i = 0; i < UTILITIES; ++i) {
        int selected = i == utilities.cursor;
        list_row(i, utility_names[i], selected, NULL, 0);
    }

}

static void draw_display(void)
{
    char value[8];
    header(0, "Display", "", 0);
    snprintf(value, sizeof value, "%u", settings_brightness());
    screen_text(&font_6_12, INSET, ROW_TOP + 4, "Brightness", LABEL);
    screen_text(&font_12_24, VALUE_RIGHT - screen_text_width(&font_12_24, value), ROW_TOP + 18, value, WHITE);
    int segments = (DISPLAY_SIDE - 2 * INSET) / SEGMENT_PITCH;
    segment_bar(INSET, ROW_TOP + 48, segments, segments * (int)settings_brightness() / DISPLAY_BRIGHTNESS_MAX, ACCENT, OFF);
}

/* Each channel's A, B and C inputs in volts, and its gate */
#define MONITOR_COLUMN_X    14
#define MONITOR_COLUMN_STEP 34
#define GATE_SIZE           7
#define MONITOR_TOP         (ROW_TOP + 9)   /* below the column labels */
#define MONITOR_LABEL_X     10              /* over the value's first digit */
static void draw_monitor(void)
{
    static const char *const columns[INPUTS_COLUMNS] = { "A", "B", "C" };
    header(0, "CV monitor", "", 0);
    for (unsigned k = 0; k < INPUTS_COLUMNS; ++k)
        screen_text(&font_5_8, MONITOR_COLUMN_X + (int)k * MONITOR_COLUMN_STEP + MONITOR_LABEL_X, ROW_TOP - 1,
                    columns[k], LABEL);
    screen_text(&font_5_8, VALUE_RIGHT - GATE_SIZE, ROW_TOP - 1, "G", LABEL);
    for (unsigned c = 0; c < CHANNELS; ++c) {
        int y = MONITOR_TOP + (int)c * ROW_HEIGHT, text_y = y + (ROW_HEIGHT - font_5_8.height) / 2;
        char n[2] = { (char)('1' + c), 0 };
        screen_text(&font_5_8, INSET - 2, text_y, n, GREY);
        for (unsigned k = 0; k < INPUTS_COLUMNS; ++k) {
            float v = inputs_cv[inputs_cv_index(c, k)];
            int cv = (int)__builtin_lrintf(v * CENTIVOLTS), magnitude = cv < 0 ? -cv : cv;
            char text[24];
            snprintf(text, sizeof text, "%c%d.%02d", cv < 0 ? '-' : '+', magnitude / 100, magnitude % 100);
            uint16_t colour = magnitude >= 5 ? WHITE : GREY;   /* above the noise: 0.05 V */
            screen_text(&font_5_8, MONITOR_COLUMN_X + (int)k * MONITOR_COLUMN_STEP, text_y, text, colour);
        }
        int gate = inputs_gates >> c & 1u;
        screen_fill(VALUE_RIGHT - GATE_SIZE + 1, y + (ROW_HEIGHT - GATE_SIZE) / 2, GATE_SIZE, GATE_SIZE, gate ? ACCENT : OFF);
    }
}

static void draw_about(void)
{
    char line[48];
    header(0, "About", "", 0);
    screen_text(&font_8_16, INSET, ROW_TOP, FIRMWARE_NAME, ACCENT);
    int y = wrapped_text(&font_5_8, INSET, ROW_TOP + 17, "Runs the Erica Synths LXR-02 engine.", LABEL);
    y = wrapped_text(&font_5_8, INSET, y, "GPL-3, unofficial", GREY) + ABOUT_GAP;
    y = wrapped_text(&font_5_8, INSET, y, FIRMWARE_AUTHOR, WHITE);
    y = wrapped_text(&font_5_8, INSET, y, GITHUB_HOST, WHITE);
    y = wrapped_text(&font_5_8, INSET, y, FIRMWARE_GITHUB, WHITE) + ABOUT_GAP;
    snprintf(line, sizeof line, "Build %02x%02x%02x%02x", identity.build_id[0], identity.build_id[1],
             identity.build_id[2], identity.build_id[3]);
    y = wrapped_text(&font_5_8, INSET, y, line, LABEL);
    snprintf(line, sizeof line, "Tempo %u BPM, %s", (unsigned)(engine_tempo() + 0.5f),
             engine_clocked() ? "MIDI clock" : "internal");
    y = wrapped_text(&font_5_8, INSET, y, line, LABEL);
    if (!(storage_mounted() & FRONT_CARD)) wrapped_text(&font_5_8, INSET, y, "No front card", ACCENT);
}

/* The name in the large font, the marked character underlined */
#define SAVE_NAME_Y         (ROW_TOP + 4)
#define SAVE_ROWS_TOP       (ROW_TOP + 3 * ROW_HEIGHT)
static void draw_save(void)
{
    static const char *const kinds[SAVE_KINDS] = { "8-channel kit", "LXR .snd" };
    char tag[16];
    if (save_page.kind == SAVE_SND) snprintf(tag, sizeof tag, "CH %u", ui.channel + 1);
    else snprintf(tag, sizeof tag, "%s", save_page.blend ? "BLEND" : "");
    header(0, save_page.blend ? "Save blend" : "Save", tag, 0);
    int x0 = (DISPLAY_SIDE - (int)KITS_KIT_NAME_BYTES * font_12_24.width) / 2;
    for (unsigned i = 0; i < KITS_KIT_NAME_BYTES; ++i) {
        char c[2] = { save_page.name[i], 0 };
        int x = x0 + (int)i * font_12_24.width, marked = save_page.naming && i == save_page.letter;
        if (c[0] == ' ') screen_fill(x + 2, SAVE_NAME_Y + font_12_24.height - 3, font_12_24.width - 4, 1, OFF);
        else screen_text(&font_12_24, x, SAVE_NAME_Y, c, marked ? ACCENT : WHITE);
        if (marked) screen_fill(x + 1, SAVE_NAME_Y + font_12_24.height, font_12_24.width - 2, 2, ACCENT);
    }
    list_row((unsigned)((SAVE_ROWS_TOP - ROW_TOP) / ROW_HEIGHT), save_page.naming ? "Push: next letter" : "Name",
             save_page.cursor == SAVE_NAME, NULL, 0);
    list_row((unsigned)((SAVE_ROWS_TOP - ROW_TOP) / ROW_HEIGHT) + 1, "Type", save_page.cursor == SAVE_KIND,
             kinds[save_page.kind], WHITE);
    list_row((unsigned)((SAVE_ROWS_TOP - ROW_TOP) / ROW_HEIGHT) + 2, "Save", save_page.cursor == SAVE_GO, NULL, 0);
}

/* The Sample inputs' levels, with their meters */
static void draw_kit_cv(void)
{
    header(0, "Kit CV", "", 0);
    for (unsigned r = 0; r < KIT_CV_ROLES; ++r) {
        char value[8];
        int k = kit_cv_choice(kit_cv_input((enum kit_cv_role)r));
        if (k) snprintf(value, sizeof value, "%u%c", (unsigned)(k - 1) / INPUTS_COLUMNS + 1, "ABC"[(k - 1) % INPUTS_COLUMNS]);
        else snprintf(value, sizeof value, "Off");
        int selected = r == kit_cv_cursor;
        list_row(r, kit_cv_names[r], selected, value, selected && ui.edit == EDIT_VALUE ? ACCENT : WHITE);
    }
    const char *folder = strrchr(quick.folder, '/');
    char line[FILES_PATH_BYTES + 48];
    snprintf(line, sizeof line, "Kits in %s, as on the Channels page", folder ? folder + 1 : quick.folder);
    wrapped_text(&font_5_8, INSET, ROW_TOP + KIT_CV_ROLES * ROW_HEIGHT + 4, line, GREY);
}

static void draw_audio_in(void)
{
    static const char *const names[2] = { "To FX bus", "To mix" };
    header(0, "Audio inputs", "", 0);
    for (unsigned r = 0; r < 2; ++r) {
        char value[8];
        snprintf(value, sizeof value, "%u%%", settings_input_level(r) * PERCENT / SETTINGS_INPUT_MAX);
        int selected = r == audio_in_cursor;
        list_row(r, names[r], selected, value, selected && ui.edit == EDIT_VALUE ? ACCENT : WHITE);
    }
    static const char *const sides[2] = { "L", "R" };
    for (unsigned side = 0; side < 2; ++side) {
        int y = ROW_TOP + (3 + (int)side) * ROW_HEIGHT;
        screen_text(&font_5_8, INSET, y + 1, sides[side], GREY);
        int lit = (int)(meters_peak[AUDIO_CHANNELS + 2u + side] * METER_GAIN * METER_SEGMENTS * 2);
        segment_bar(INSET + 10, y + (ROW_HEIGHT - BAR_HEIGHT) / 2, METER_SEGMENTS * 2, lit, ACCENT, OFF);
    }
}

static void draw_channel(void)
{
    static const char *const names[CHANNEL_ROWS] = { "Pitch CV", "Accent CV" };
    static const char *const notes[CHANNEL_ROWS] = { "12 semitones a volt", "0-5 V sets gate velocity" };
    static const char columns[INPUTS_COLUMNS] = { 'A', 'B', 'C' };
    int v = voice();
    header((int)ui.channel + 1, v >= 0 ? voice_names[v] : "Empty", "INPUTS", 0);
    for (unsigned r = 0; r < CHANNEL_ROWS; ++r) {
        int column = channel_column(r);
        char value[4] = { 0 };
        if (column) value[0] = columns[column - 1];
        int selected = r == channel_cursor;
        list_row(r, names[r], selected, column ? value : "Off",
                 selected && ui.edit == EDIT_VALUE ? ACCENT : column ? WHITE : GREY);
    }
    screen_text(&font_5_8, INSET, ROW_TOP + CHANNEL_ROWS * ROW_HEIGHT + 4, notes[channel_cursor], GREY);
}

/* The amount as set (bar) and with CV (marker), then the rows */
#define MORPH_BAR_Y         (ROW_TOP + 22)
#define MORPH_ROWS_TOP      (ROW_TOP + 40)
static void draw_morph(void)
{
    char value[24], source[8];
    unsigned set = engine_morph_value(), live = engine_morph_amount();
    header(0, "Morph", ui.cv_view ? "CV" : "", ui.cv_view);
    snprintf(value, sizeof value, "%u%%", set * PERCENT / ENGINE_MORPH_MAX);
    screen_text(&font_6_12, INSET, ROW_TOP + 2, morph_page.kit[0] ? morph_page.kit : "No morph kit",
                morph_page.kit[0] ? LABEL : GREY);
    screen_text(&font_8_16, VALUE_RIGHT - screen_text_width(&font_8_16, value), ROW_TOP, value,
                ui.edit == EDIT_VALUE ? ACCENT : WHITE);
    int segments = (DISPLAY_SIDE - 2 * INSET) / SEGMENT_PITCH;
    segment_bar(INSET, MORPH_BAR_Y, segments, segments * (int)set / ENGINE_MORPH_MAX, ACCENT, OFF);
    int marker = INSET + (segments * SEGMENT_PITCH - 1) * (int)live / ENGINE_MORPH_MAX;
    screen_fill(marker, MORPH_BAR_Y - 3, 1, BAR_HEIGHT + 6, morph_route.source ? AMBER : WHITE);
    if (ui.cv_view) {
        cv_source_text(morph_route.source, source, sizeof source);
        snprintf(value, sizeof value, "%s %+d", source, morph_route.amount);
        int y = MORPH_ROWS_TOP;
        list_row((unsigned)((y - ROW_TOP) / ROW_HEIGHT), "Amount CV", 1, morph_route.source ? value : "--",
                 ui.edit != EDIT_NONE ? AMBER : morph_route.source ? AMBER : GREY);
        if (ui.edit == EDIT_CV_SOURCE && morph_route.source) {
            volts_text(inputs_cv[cv_input(0, morph_route.source)], value, sizeof value);
            screen_text(&font_5_8, INSET, y + ROW_HEIGHT + 3, value, AMBER);
        }
        return;
    }
    for (unsigned i = 0; i < MORPH_ROWS; ++i)
        list_row((unsigned)((MORPH_ROWS_TOP - ROW_TOP) / ROW_HEIGHT) + i, morph_rows[i], i == morph_page.cursor, NULL, 0);
}

/* A note over the bottom of the page */
#define MESSAGE_HEIGHT      16
static void draw_message(void)
{
    if ((int32_t)(ui.message_until - ui.now_ms) <= 0) return;
    int y = FOOTER_TOP - MESSAGE_HEIGHT - 2, w = screen_text_width(&font_6_12, ui.message) + 2 * INSET;
    int x = (DISPLAY_SIDE - w) / 2;
    screen_fill(x - 1, y - 1, w + 2, MESSAGE_HEIGHT + 2, BLACK);
    screen_fill(x, y, w, MESSAGE_HEIGHT, ACCENT);
    screen_fill(x + 1, y + 1, w - 2, MESSAGE_HEIGHT - 2, PANEL);
    screen_text(&font_6_12, x + INSET, y + (MESSAGE_HEIGHT - font_6_12.height) / 2, ui.message, WHITE);
}

/* The start-up screen marks the firmware as unofficial (LXR licence, 7(c)) */
static void draw_splash(void)
{
    screen_text(&font_12_24, (DISPLAY_SIDE - screen_text_width(&font_12_24, FIRMWARE_NAME)) / 2, SPLASH_NAME_Y,
                FIRMWARE_NAME, ACCENT);
    int y = wrapped_text(&font_6_12, INSET, SPLASH_TEXT_Y, "Unofficial firmware", WHITE) + 2;
    wrapped_text(&font_5_8, INSET, y, "Runs the Erica Synths LXR-02 sound engine. Not made or approved by "
                 "Erica Synths or Rossum Electro-Music.", LABEL);
    screen_text(&font_5_8, INSET, SPLASH_AUTHOR_Y - font_5_8.height - 1, GITHUB_HOST, WHITE);
    screen_text(&font_5_8, INSET, SPLASH_AUTHOR_Y, FIRMWARE_GITHUB, WHITE);
    char line[24];
    snprintf(line, sizeof line, "GPL-3  build %02x%02x%02x%02x", identity.build_id[0], identity.build_id[1],
             identity.build_id[2], identity.build_id[3]);
    screen_text(&font_5_8, INSET, SPLASH_AUTHOR_Y + font_5_8.height + 2, line, GREY);
}

void ui_draw(void)
{
    screen_clear();
    if (!ui.splash_done && ui.now_ms < SPLASH_MS) {
        draw_splash();
        if (fault[0]) screen_text(&font_5_8, INSET, DISPLAY_SIDE - font_5_8.height, fault, ACCENT);
        return;
    }
    ui.splash_done = 1;
    switch (ui.view) {
    case VIEW_CHANNELS: draw_channels(); break;
    case VIEW_MUTATE: draw_mutate(); break;
    case VIEW_INSTRUMENT: draw_instrument(); break;
    case VIEW_MIXER: draw_mixer(); break;
    case VIEW_FILES: draw_files(); break;
    case VIEW_SETTINGS: draw_settings(); break;
    case VIEW_UTILITIES: draw_utilities(); break;
    case VIEW_DISPLAY: draw_display(); break;
    case VIEW_MONITOR: draw_monitor(); break;
    case VIEW_ABOUT: draw_about(); break;
    case VIEW_MORPH: draw_morph(); break;
    case VIEW_CHANNEL: draw_channel(); break;
    case VIEW_AUDIO_IN: draw_audio_in(); break;
    case VIEW_KIT_CV: draw_kit_cv(); break;
    case VIEW_SAVE: draw_save(); break;
    case VIEW_FIRMWARE: draw_firmware(); break;
    default: draw_page(); break;
    }
    activity_footer();
    draw_message();
    if (fault[0]) screen_text(&font_5_8, INSET, DISPLAY_SIDE - font_5_8.height, fault, ACCENT);
}
