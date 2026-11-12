/* MIDI settings files.
 *
 * glo.cfg is the LXR-02's global parameter block: one byte per parameter
 * from PAR_BEGINNING_OF_GLOBALS. Of it this firmware uses the voice MIDI
 * channels (1-16), their notes (0 any) and the global channel (1-16, 0
 * off), and writes back the rest as it was read.
 *
 * SETTINGS_FILE holds the Chroma 8 channel and the General MIDI drum
 * channel (1-16, 0 off), the display brightness (1-16, 0 full), and the
 * Sample inputs' levels to the FX bus and to the mix (0-127), and the
 * rest listed at the EXTRA_ enum. A
 * shorter file reads as if padded with zeros. */
#include "settings.h"
#include "kits.h"
#include "Parameters.h"
#include "ff.h"
#include "display.h"
#include "kit_cv.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define GLOBALS_FILE        KITS_FOLDER "/glo.cfg"
#define SETTINGS_FILE       KITS_FOLDER "/AssimiLXR.cfg"
#define GLOBALS             (NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS)
#define GLOBAL(p)           ((p) - PAR_BEGINNING_OF_GLOBALS)
#define CHANNEL_OFF         0u      /* in the files: channels are 1-16 */
#define MIDI_CHANNELS       16u
#define LXR_DEFAULT_BPM     120u

/* SETTINGS_FILE: a byte each, then the kit CV inputs (an inputs_cv index
 * plus 1, 0 for off), the kits folder, zero-padded, velocity always
 * setting the level (0 as the kit says), and the last kit loaded from that
 * folder, zero-padded (none if empty) */
enum { EXTRA_CHROMA, EXTRA_DRUMS, EXTRA_BRIGHTNESS, EXTRA_INPUT_SEND, EXTRA_INPUT_DRY,
       EXTRA_KIT_CV, EXTRA_KITS_FOLDER = EXTRA_KIT_CV + KIT_CV_ROLES,
       EXTRA_VELOCITY = EXTRA_KITS_FOLDER + KITS_PATH_BYTES, EXTRA_KIT_NAME,
       EXTRAS = EXTRA_KIT_NAME + KITS_NAME_BYTES };

static char last_kit[KITS_NAME_BYTES];

static uint8_t input_levels[2];     /* to the FX bus, to the mix; 0-127 */

uint8_t settings_input_level(unsigned which) { return input_levels[which]; }

void settings_set_input_level(unsigned which, uint8_t level)
{
    input_levels[which] = level > SETTINGS_INPUT_MAX ? SETTINGS_INPUT_MAX : level;
    engine_inputs_audio(input_levels[SETTINGS_INPUT_SEND], input_levels[SETTINGS_INPUT_DRY]);
}

static unsigned brightness = DISPLAY_BRIGHTNESS_MAX;

unsigned settings_brightness(void) { return brightness; }

void settings_set_brightness(unsigned level)
{
    brightness = level < 1 ? 1 : level > DISPLAY_BRIGHTNESS_MAX ? DISPLAY_BRIGHTNESS_MAX : level;
    display_brightness(brightness);
}

static uint8_t globals[GLOBALS];

/* The LXR keeps the open hi-hat's channel apart from the other six */
static unsigned channel_parameter(unsigned voice)
{
    return voice < ENGINE_TRIGGERS - 1 ? PAR_MIDI_CHAN_1 + voice : PAR_MIDI_CHAN_7;
}

static uint8_t from_file(uint8_t channel)
{
    return channel == CHANNEL_OFF || channel > MIDI_CHANNELS ? ENGINE_MIDI_OFF : (uint8_t)(channel - 1u);
}

static uint8_t to_file(uint8_t channel)
{
    return channel == ENGINE_MIDI_OFF ? CHANNEL_OFF : (uint8_t)(channel + 1u);
}

static int read_bytes(const char *path, uint8_t *data, UINT size, int exact)
{
    FIL file;
    UINT got = 0;
    memset(data, 0, size);
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    FRESULT result = f_read(&file, data, size, &got);
    f_close(&file);
    return result == FR_OK && (got == size || !exact);
}

static int write_bytes(const char *path, const uint8_t *data, UINT size)
{
    FIL file;
    UINT written = 0;
    if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    FRESULT result = f_write(&file, data, size, &written);
    return f_close(&file) == FR_OK && result == FR_OK && written == size;
}

void settings_load(void)
{
    struct engine_midi_setup m;
    engine_midi_setup_get(&m);
    globals[GLOBAL(PAR_BPM)] = LXR_DEFAULT_BPM;
    if (kits_folder() && read_bytes(GLOBALS_FILE, globals, sizeof globals, 1)) {
        for (unsigned v = 0; v < ENGINE_TRIGGERS; ++v) {
            m.voice_channel[v] = from_file(globals[GLOBAL(channel_parameter(v))]);
            m.voice_note[v] = globals[GLOBAL(PAR_MIDI_NOTE1 + v)];
        }
        m.global_channel = from_file(globals[GLOBAL(PAR_MIDI_CHAN_GLOBAL)]);
    }
    uint8_t extras[EXTRAS];
    if (read_bytes(SETTINGS_FILE, extras, sizeof extras, 0)) {
        m.chroma_channel = from_file(extras[EXTRA_CHROMA]);
        m.drum_channel = from_file(extras[EXTRA_DRUMS]);
        settings_set_brightness(extras[EXTRA_BRIGHTNESS] ? extras[EXTRA_BRIGHTNESS] : DISPLAY_BRIGHTNESS_MAX);
        settings_set_input_level(SETTINGS_INPUT_SEND, extras[EXTRA_INPUT_SEND]);
        settings_set_input_level(SETTINGS_INPUT_DRY, extras[EXTRA_INPUT_DRY]);
        for (unsigned r = 0; r < KIT_CV_ROLES; ++r)
            kit_cv_set_input((enum kit_cv_role)r, extras[EXTRA_KIT_CV + r] ? extras[EXTRA_KIT_CV + r] - 1u : KIT_CV_OFF);
        m.velocity_always = extras[EXTRA_VELOCITY] != 0;
        extras[EXTRA_VELOCITY - 1] = 0;
        if (extras[EXTRA_KITS_FOLDER]) ui_set_kits_folder((const char *)&extras[EXTRA_KITS_FOLDER]);
        memcpy(last_kit, &extras[EXTRA_KIT_NAME], KITS_NAME_BYTES - 1);
    }
    engine_midi_setup(&m);
}

int settings_save(void)
{
    struct engine_midi_setup m;
    engine_midi_setup_get(&m);
    for (unsigned v = 0; v < ENGINE_TRIGGERS; ++v) {
        globals[GLOBAL(channel_parameter(v))] = to_file(m.voice_channel[v]);
        globals[GLOBAL(PAR_MIDI_NOTE1 + v)] = m.voice_note[v];
    }
    globals[GLOBAL(PAR_MIDI_CHAN_GLOBAL)] = to_file(m.global_channel);
    uint8_t extras[EXTRAS] = { [EXTRA_CHROMA] = to_file(m.chroma_channel), [EXTRA_DRUMS] = to_file(m.drum_channel),
                               [EXTRA_BRIGHTNESS] = (uint8_t)brightness,
                               [EXTRA_INPUT_SEND] = input_levels[SETTINGS_INPUT_SEND],
                               [EXTRA_INPUT_DRY] = input_levels[SETTINGS_INPUT_DRY],
                               [EXTRA_VELOCITY] = m.velocity_always };
    for (unsigned r = 0; r < KIT_CV_ROLES; ++r) {
        uint8_t input = kit_cv_input((enum kit_cv_role)r);
        extras[EXTRA_KIT_CV + r] = input == KIT_CV_OFF ? 0 : (uint8_t)(input + 1u);
    }
    strncpy((char *)&extras[EXTRA_KITS_FOLDER], ui_kits_folder(), KITS_PATH_BYTES - 1);
    strncpy((char *)&extras[EXTRA_KIT_NAME], ui_kit_name(), KITS_NAME_BYTES - 1);
    return kits_folder() && write_bytes(GLOBALS_FILE, globals, sizeof globals) &&
           write_bytes(SETTINGS_FILE, extras, sizeof extras);
}

void settings_load_kit(void)
{
    if (!last_kit[0]) return;
    char path[KITS_PATH_BYTES + KITS_NAME_BYTES + 1];
    snprintf(path, sizeof path, "%s/%s", ui_kits_folder(), last_kit);
    if (kits_load(path, KITS_ALL_CHANNELS, 0)) ui_kit_loaded(ui_kits_folder(), last_kit);
}
