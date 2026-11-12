/* Kit files.
 *
 * An LXR-02 kit (.snd) is an 8-byte name, then one byte per sound
 * parameter in the LXR's order; a shorter file leaves the rest 0. It holds
 * all six voices, so any channel can take its sound for the voice it
 * plays.
 *
 * A .kit holds the eight channels: a header (with the morph amount and its
 * CV route), then one record a channel (voice, mute, FX send, the sound
 * parameters, the CV routes, the morph kit, the pitch and accent inputs,
 * the play mode). The header gives its own size
 * and the record and parameter counts, so a file from an older build opens
 * with the fields it lacks set to 0. */
#include "kits.h"
#include "engine.h"
#include "ui.h"
#include "inputs.h"
#include "Parameters.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define SND_NAME_BYTES      8u
#define KIT_MAGIC           "ALXRKIT"
#define KIT_FORMAT          2u
#define KIT_CHANNELS        ENGINE_CHANNELS
#define KIT_NUMBER_MAX      999u
#define PARAMETERS          END_OF_SOUND_PARAMETERS

struct kit_header {
    char magic[8];
    uint16_t format, header_bytes, channel_bytes, parameters;
    uint8_t channels, morph;
    struct cv_route morph_route;
};

struct kit_channel {
    uint8_t voice, mute, send, has_morph;
    uint8_t parameters[PARAMETERS];
    struct cv_route routes[PARAMETERS];
    uint8_t morph[PARAMETERS];
    uint8_t pitch_column, accent_column;    /* 0 off, 1-3 the channel's A-C */
    uint8_t gated;                          /* play mode: 0 1 Shot, 1 Gated */
};

/* The largest file read: a .kit of this build's size */
static uint8_t file_buffer[sizeof(struct kit_header) + KIT_CHANNELS * sizeof(struct kit_channel)];

int kits_folder(void)
{
    FRESULT result = f_mkdir(KITS_FOLDER);
    return result == FR_OK || result == FR_EXIST;
}

static int ends_with(const char *path, const char *extension)
{
    size_t n = strlen(path), e = strlen(extension);
    if (n < e) return 0;
    for (size_t i = 0; i < e; ++i) {
        char c = path[n - e + i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != extension[i]) return 0;
    }
    return 1;
}

static int before(const struct kits_entry *a, const struct kits_entry *b)
{
    if (a->folder != b->folder) return a->folder;
    return strcasecmp(a->name, b->name) < 0;
}

unsigned kits_list(const char *folder, struct kits_entry *entries, unsigned max)
{
    DIR dir;
    FILINFO info;
    unsigned count = 0;
    if (f_opendir(&dir, folder) != FR_OK) return 0;
    while (count < max && f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        if (info.fattrib & (AM_HID | AM_SYS)) continue;
        int is_folder = (info.fattrib & AM_DIR) != 0;
        if (!is_folder && !ends_with(info.fname, ".snd") && !ends_with(info.fname, ".kit")) continue;
        if (is_folder && !strcmp(folder, KITS_FOLDER) &&
            (!strcasecmp(info.fname, KITS_OSC_WAVES) || !strcasecmp(info.fname, KITS_TRANSIENT_WAVES))) continue;
        size_t length = strlen(info.fname);
        if (length >= KITS_NAME_BYTES) continue;    /* too long to open by name */
        struct kits_entry e = { .folder = (uint8_t)is_folder };
        memcpy(e.name, info.fname, length + 1);
        unsigned i = count++;
        for (; i > 0 && before(&e, &entries[i - 1]); --i) entries[i] = entries[i - 1];
        entries[i] = e;
    }
    f_closedir(&dir);
    return count;
}

static int read_file(const char *path, uint8_t *buffer, uint32_t *bytes)
{
    FIL file;
    UINT got = 0;
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    FRESULT result = f_read(&file, buffer, sizeof file_buffer, &got);
    f_close(&file);
    *bytes = got;
    return result == FR_OK;
}

static int load_snd(const uint8_t *file, uint32_t bytes, int channel, int into_morph)
{
    if (bytes <= SND_NAME_BYTES) return 0;
    const uint8_t *parameters = file + SND_NAME_BYTES;
    uint32_t count = bytes - SND_NAME_BYTES;
    for (unsigned c = 0; c < KIT_CHANNELS; ++c) {
        if (channel != KITS_ALL_CHANNELS && (unsigned)channel != c) continue;
        int ok = into_morph ? engine_load_morph(c, parameters, count) :
                 engine_load_channel(c, parameters, count, ENGINE_KEEP_VOICE, engine_mix_value(c, ENGINE_MIX_MUTE),
                                     engine_mix_value(c, ENGINE_MIX_SEND));
        if (!ok) return 0;
    }
    return 1;
}

/* A record's field, or 0 where the file's record ends before it */
static const uint8_t *field(const uint8_t *record, uint32_t record_bytes, uint32_t offset, uint32_t size)
{
    static const uint8_t zeros[PARAMETERS * sizeof(struct cv_route)];
    return offset + size <= record_bytes ? record + offset : zeros;
}

static uint8_t column_input(unsigned channel, uint8_t column)
{
    return column && column <= INPUTS_COLUMNS ? (uint8_t)inputs_cv_index(channel, column - 1u) : ENGINE_NO_INPUT;
}

static uint8_t input_column(uint8_t input)
{
    return input == ENGINE_NO_INPUT ? 0 : (uint8_t)(input / INPUTS_CHANNELS + 1u);
}

static int load_kit(const uint8_t *file, uint32_t bytes, int channel, int into_morph)
{
    struct kit_header h = { 0 };
    const uint32_t fixed = offsetof(struct kit_header, header_bytes) + sizeof h.header_bytes;
    if (bytes < fixed) return 0;
    memcpy(&h, file, fixed);
    if (memcmp(h.magic, KIT_MAGIC, sizeof h.magic) || h.format != KIT_FORMAT || h.header_bytes > bytes) return 0;
    memcpy(&h, file, h.header_bytes < sizeof h ? h.header_bytes : sizeof h);
    if (bytes < h.header_bytes + (uint32_t)h.channels * h.channel_bytes) return 0;
    uint32_t count = h.parameters < PARAMETERS ? h.parameters : PARAMETERS;
    const uint32_t offset_parameters = 4u, offset_routes = offset_parameters + h.parameters,
                   offset_morph = offset_routes + h.parameters * (uint32_t)sizeof(struct cv_route);
    for (unsigned c = 0; c < h.channels && c < KIT_CHANNELS; ++c) {
        if (channel != KITS_ALL_CHANNELS && (unsigned)channel != c) continue;
        const uint8_t *record = file + h.header_bytes + c * h.channel_bytes;
        const uint8_t *head = field(record, h.channel_bytes, 0, offset_parameters);
        const uint8_t *parameters = field(record, h.channel_bytes, offset_parameters, h.parameters);
        const uint8_t *morph = field(record, h.channel_bytes, offset_morph, h.parameters);
        if (into_morph) {
            if (!engine_load_morph(c, parameters, count)) return 0;
            continue;
        }
        if (!engine_load_channel(c, parameters, count, head[0], head[1], head[2]) ||
            !engine_load_morph(c, morph, head[3] ? count : 0)) return 0;
        const uint8_t *inputs = field(record, h.channel_bytes, offset_morph + h.parameters, 3);
        engine_channel_inputs(c, column_input(c, inputs[0]), column_input(c, inputs[1]));
        engine_mix(c, ENGINE_MIX_GATED, inputs[2]);
        struct cv_route *mine = ui_cv_routes(c);
        memset(mine, 0, PARAMETERS * sizeof *mine);
        memcpy(mine, field(record, h.channel_bytes, offset_routes, h.parameters * (uint32_t)sizeof *mine),
               count * sizeof *mine);
    }
    if (!into_morph && channel == KITS_ALL_CHANNELS) {
        engine_morph(h.morph);
        *ui_morph_route() = h.morph_route;
    }
    ui_cv_routes_changed();
    return 1;
}

static int load_bytes(const char *name, const uint8_t *file, uint32_t bytes, int channel, int into_morph)
{
    if (ends_with(name, ".snd")) return load_snd(file, bytes, channel, into_morph);
    if (ends_with(name, ".kit")) return load_kit(file, bytes, channel, into_morph);
    return 0;
}

int kits_load(const char *path, int channel, int into_morph)
{
    uint32_t bytes;
    if (!kits_folder() || !read_file(path, file_buffer, &bytes)) return 0;
    return load_bytes(path, file_buffer, bytes, channel, into_morph);
}

/* The kits of one folder, read ahead so kit CV loads them from memory */
static struct {
    uint8_t file[sizeof file_buffer];
    uint32_t bytes;
    char name[KITS_NAME_BYTES];
} cache[KITS_CACHE];
static unsigned cached;

unsigned kits_cache(const char *folder, const struct kits_entry *kits, unsigned count)
{
    cached = 0;
    for (unsigned i = 0; i < count && cached < KITS_CACHE; ++i) {
        char path[KITS_PATH_BYTES];
        if (snprintf(path, sizeof path, "%s/%s", folder, kits[i].name) >= (int)sizeof path ||
            !read_file(path, cache[cached].file, &cache[cached].bytes)) cache[cached].bytes = 0;
        memcpy(cache[cached].name, kits[i].name, KITS_NAME_BYTES);
        cached++;
    }
    return cached;
}

int kits_load_cached(unsigned index)
{
    if (index >= cached) return 0;
    return load_bytes(cache[index].name, cache[index].file, cache[index].bytes, KITS_ALL_CHANNELS, 0);
}

/* A file name from a kit name: letters, digits, - and _, spaces as _,
 * trailing spaces dropped */
static void file_stem(const char *name, unsigned max, char *out)
{
    unsigned n = 0, last = 0;
    for (unsigned i = 0; name[i] && n < max; ++i) {
        char c = name[i];
        int keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        out[n++] = keep ? c : '_';
        if (c != ' ') last = n;
    }
    out[last] = 0;
}

int kits_save(const char *kit_name, int blend, char *saved, size_t size)
{
    if (!kits_folder()) return 0;
    char path[sizeof KITS_FOLDER + 32], stem[KITS_KIT_NAME_BYTES + 1];
    file_stem(kit_name, KITS_KIT_NAME_BYTES, stem);
    if (stem[0]) snprintf(path, sizeof path, "%s/%s.kit", KITS_FOLDER, stem);
    else {
        FILINFO info;
        unsigned number = 1;
        for (; number <= KIT_NUMBER_MAX; ++number) {
            snprintf(path, sizeof path, "%s/KIT%03u.kit", KITS_FOLDER, number);
            if (f_stat(path, &info) == FR_NO_FILE) break;
        }
        if (number > KIT_NUMBER_MAX) return 0;
        snprintf(stem, sizeof stem, "KIT%03u", number);
    }

    struct kit_header h = { .format = KIT_FORMAT, .header_bytes = sizeof h,
                            .channel_bytes = sizeof(struct kit_channel), .parameters = PARAMETERS,
                            .channels = KIT_CHANNELS, .morph = blend ? 0 : engine_morph_value(),
                            .morph_route = blend ? (struct cv_route){ 0 } : *ui_morph_route() };
    memcpy(h.magic, KIT_MAGIC, sizeof h.magic);
    memcpy(file_buffer, &h, sizeof h);
    for (unsigned c = 0; c < KIT_CHANNELS; ++c) {
        struct kit_channel *k = (struct kit_channel *)(file_buffer + sizeof h) + c;
        int voice = engine_voice(c);
        k->voice = voice < 0 ? ENGINE_NO_VOICE : (uint8_t)voice;
        k->mute = engine_mix_value(c, ENGINE_MIX_MUTE);
        k->send = engine_mix_value(c, ENGINE_MIX_SEND);
        const uint8_t *morph = engine_channel_morph(c);
        k->has_morph = !blend && morph;
        if (blend) engine_channel_blend(c, k->parameters);
        else memcpy(k->parameters, engine_channel_parameters(c), PARAMETERS);
        memcpy(k->routes, ui_cv_routes(c), sizeof k->routes);
        if (k->has_morph) memcpy(k->morph, morph, PARAMETERS);
        else memset(k->morph, 0, PARAMETERS);
        uint8_t pitch, accent;
        engine_channel_inputs_get(c, &pitch, &accent);
        k->pitch_column = input_column(pitch);
        k->accent_column = input_column(accent);
        k->gated = engine_mix_value(c, ENGINE_MIX_GATED);
    }
    FIL file;
    UINT written = 0;
    if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    FRESULT result = f_write(&file, file_buffer, sizeof file_buffer, &written);
    if (f_close(&file) != FR_OK || result != FR_OK || written != sizeof file_buffer) return 0;
    snprintf(saved, size, "%s.kit", stem);
    return 1;
}

/* As the LXR-02 names kits: NN-XXXXX.snd, the next free number and the
 * first five characters of the name; inside, the name padded to 8 */
#define SND_NUMBERS         64u
#define SND_STEM_CHARS      5u
int kits_save_snd(unsigned channel, const char *kit_name, int blend, char *saved, size_t size)
{
    if (!kits_folder()) return 0;
    char stem[SND_STEM_CHARS + 1], path[sizeof KITS_FOLDER + 32];
    file_stem(kit_name, SND_STEM_CHARS, stem);
    DIR dir;
    FILINFO info;
    uint64_t used = 0;
    if (f_opendir(&dir, KITS_FOLDER) == FR_OK) {
        while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
            const char *n = info.fname;
            if (n[0] >= '0' && n[0] <= '9' && n[1] >= '0' && n[1] <= '9' && n[2] == '-' && ends_with(n, ".snd"))
                used |= 1ull << ((n[0] - '0') * 10 + (n[1] - '0')) % SND_NUMBERS;
        }
        f_closedir(&dir);
    }
    unsigned number = 0;
    while (number < SND_NUMBERS && (used >> number & 1u)) number++;
    if (number == SND_NUMBERS) return 0;
    snprintf(path, sizeof path, "%s/%02u-%s.snd", KITS_FOLDER, number, stem);
    uint8_t *out = file_buffer;
    memset(out, ' ', SND_NAME_BYTES);
    for (unsigned i = 0; i < SND_NAME_BYTES && kit_name[i]; ++i) out[i] = (uint8_t)kit_name[i];
    if (blend) engine_channel_blend(channel, out + SND_NAME_BYTES);
    else memcpy(out + SND_NAME_BYTES, engine_channel_parameters(channel), PARAMETERS);
    FIL file;
    UINT written = 0;
    if (f_open(&file, path, FA_WRITE | FA_CREATE_NEW) != FR_OK) return 0;
    FRESULT result = f_write(&file, out, SND_NAME_BYTES + PARAMETERS, &written);
    if (f_close(&file) != FR_OK || result != FR_OK || written != SND_NAME_BYTES + PARAMETERS) return 0;
    snprintf(saved, size, "%02u-%s.snd", number, stem);
    return 1;
}
