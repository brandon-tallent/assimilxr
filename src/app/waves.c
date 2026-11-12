/* User waves from the front card.
 *
 * WAV files in WAVES_OSC_FOLDER are single cycles: each is resampled to
 * USER_WAVE_POINTS, its harmonics found by a DFT, and each octave's table
 * rebuilt with the harmonics below Nyquist at the top note of that octave,
 * so the LXR's octave table switching keeps it alias-free. WAV files in
 * WAVES_TRANSIENT_FOLDER are one-shots, resampled to 48 kHz and cut at
 * USER_TRANSIENT_MAX. Files take slots in name order; the first
 * USER_WAVES of each folder are used. */
#include "waves.h"
#include "kits.h"
#include "audio.h"
#include "user_waves.h"
#include "ff.h"
#include <math.h>
#include <string.h>
#include <strings.h>

#define WAVES_OSC_FOLDER        KITS_FOLDER "/" KITS_OSC_WAVES
#define WAVES_TRANSIENT_FOLDER  KITS_FOLDER "/" KITS_TRANSIENT_WAVES
#define WAV_HEADER_BYTES        12u
#define CHUNK_HEADER_BYTES      8u
#define FORMAT_PCM              1u
#define FORMAT_FLOAT            3u
#define FORMAT_EXTENSIBLE       0xfffeu
#define READ_BYTES              (1u << 20)
#define HARMONICS               (USER_WAVE_POINTS / 2u - 1u)
#define NYQUIST_HZ              (AUDIO_RATE / 2.0f)
#define A4_HZ                   440.0f
#define A4_NOTE                 69
#define NOTES_PER_OCTAVE        12
#define TABLE_PEAK              32000.0f
#define TWO_PI                  6.28318530718f

static char names[2][USER_WAVES][WAVES_NAME_BYTES];
static unsigned counts[2];
static uint8_t file_data[READ_BYTES];
static float mono[USER_TRANSIENT_MAX];
static float cycle[USER_WAVE_POINTS], cosines[USER_WAVE_POINTS], sines[USER_WAVE_POINTS];
static float harmonic_cos[HARMONICS + 1], harmonic_sin[HARMONICS + 1];

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Reads a WAV into mono floats; returns the frames, 0 if unusable */
static uint32_t read_wav(const char *path, uint32_t max_frames, uint32_t *rate)
{
    FIL file;
    UINT got = 0;
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    FRESULT result = f_read(&file, file_data, sizeof file_data, &got);
    f_close(&file);
    if (result != FR_OK || got < WAV_HEADER_BYTES || memcmp(file_data, "RIFF", 4) || memcmp(file_data + 8, "WAVE", 4))
        return 0;
    uint16_t format = 0, channels = 0, bits = 0;
    const uint8_t *data = 0;
    uint32_t data_bytes = 0;
    for (uint32_t at = WAV_HEADER_BYTES; at + CHUNK_HEADER_BYTES <= got;) {
        uint32_t size = le32(file_data + at + 4);
        const uint8_t *body = file_data + at + CHUNK_HEADER_BYTES;
        if (!memcmp(file_data + at, "fmt ", 4) && size >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            *rate = le32(body + 4);
            bits = le16(body + 14);
            if (format == FORMAT_EXTENSIBLE && size >= 26) format = le16(body + 24);
        } else if (!memcmp(file_data + at, "data", 4)) {
            data = body;
            data_bytes = size < got - (at + CHUNK_HEADER_BYTES) ? size : got - (at + CHUNK_HEADER_BYTES);
            break;
        }
        at += CHUNK_HEADER_BYTES + size + (size & 1u);
    }
    unsigned bytes = bits / 8u;
    if (!data || !channels || !*rate || (format != FORMAT_PCM && format != FORMAT_FLOAT) ||
        (format == FORMAT_PCM && bytes != 2 && bytes != 3 && bytes != 4) || (format == FORMAT_FLOAT && bytes != 4))
        return 0;
    uint32_t frames = data_bytes / (bytes * channels);
    if (frames > max_frames) frames = max_frames;
    for (uint32_t f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (unsigned c = 0; c < channels; ++c) {
            const uint8_t *p = data + (f * channels + c) * bytes;
            float v;
            if (format == FORMAT_FLOAT) { uint32_t w = le32(p); memcpy(&v, &w, sizeof v); }
            else if (bytes == 2) v = (int16_t)le16(p) / 32768.0f;
            else if (bytes == 3) v = (int32_t)((uint32_t)(p[0] << 8 | p[1] << 16 | (uint32_t)p[2] << 24)) / 2147483648.0f;
            else v = (int32_t)le32(p) / 2147483648.0f;
            sum += v;
        }
        mono[f] = sum / channels;
    }
    return frames;
}

/* Linear resampling of mono[0..frames) to count points spanning it */
static float sample_at(uint32_t frames, float position)
{
    uint32_t i = (uint32_t)position;
    float frac = position - (float)i;
    float a = mono[i < frames ? i : frames - 1], b = mono[i + 1 < frames ? i + 1 : frames - 1];
    return a + (b - a) * frac;
}

static float octave_top_hz(unsigned table)
{
    int note = (int)table * NOTES_PER_OCTAVE + NOTES_PER_OCTAVE - 1;
    return A4_HZ * powf(2.0f, (float)(note - A4_NOTE) / NOTES_PER_OCTAVE);
}

static void build_wave(unsigned slot, uint32_t frames)
{
    for (unsigned i = 0; i < USER_WAVE_POINTS; ++i)
        cycle[i] = sample_at(frames, (float)i * frames / USER_WAVE_POINTS);
    for (unsigned h = 1; h <= HARMONICS; ++h) {
        float c = 0.0f, s = 0.0f;
        for (unsigned i = 0; i < USER_WAVE_POINTS; ++i) {
            unsigned k = (h * i) & (USER_WAVE_POINTS - 1u);  /* a power of two: no divide */
            c += cycle[i] * cosines[k];
            s += cycle[i] * sines[k];
        }
        harmonic_cos[h] = c * (2.0f / USER_WAVE_POINTS);
        harmonic_sin[h] = s * (2.0f / USER_WAVE_POINTS);
    }
    user_wave_table *tables = user_wave_slot(slot);
    float scale = 0.0f;
    for (unsigned t = 0; t < USER_WAVE_TABLES; ++t) {
        unsigned top = (unsigned)(NYQUIST_HZ / octave_top_hz(t));
        top = top < 1 ? 1 : top > HARMONICS ? HARMONICS : top;
        float peak = 0.0f;
        for (unsigned i = 0; i < USER_WAVE_POINTS; ++i) {
            float v = 0.0f;
            for (unsigned h = 1; h <= top; ++h) {
                unsigned k = (h * i) & (USER_WAVE_POINTS - 1u);  /* a power of two: no divide */
                v += harmonic_cos[h] * cosines[k] + harmonic_sin[h] * sines[k];
            }
            cycle[i] = v;
            peak = fabsf(v) > peak ? fabsf(v) : peak;
        }
        if (t == 0) scale = peak > 0.0f ? TABLE_PEAK / peak : 0.0f;  /* every octave at the same level */
        for (unsigned i = 0; i < USER_WAVE_POINTS; ++i) {
            float v = cycle[i] * scale;
            (*tables)[t][i] = (int16_t)(v > TABLE_PEAK ? TABLE_PEAK : v < -TABLE_PEAK ? -TABLE_PEAK : v);
        }
    }
    user_wave_set(slot, 1);
}

static void build_transient(unsigned slot, uint32_t frames, uint32_t rate)
{
    float step = (float)rate / AUDIO_RATE;
    uint32_t length = (uint32_t)(frames / step);
    if (length > USER_TRANSIENT_MAX) length = USER_TRANSIENT_MAX;
    int16_t *out = user_transient_slot(slot);
    user_transient_set(slot, 0);
    for (uint32_t i = 0; i < length; ++i) {
        float v = sample_at(frames, i * step) * TABLE_PEAK;
        out[i] = (int16_t)(v > TABLE_PEAK ? TABLE_PEAK : v < -TABLE_PEAK ? -TABLE_PEAK : v);
    }
    user_transient_set(slot, length);
}

static int is_wav(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".wav");
}

/* The folder's WAV names, sorted, at most USER_WAVES */
static unsigned list(const char *folder, char out[USER_WAVES][WAVES_NAME_BYTES])
{
    DIR dir;
    FILINFO info;
    unsigned count = 0;
    if (f_opendir(&dir, folder) != FR_OK) return 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        if ((info.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !is_wav(info.fname) ||
            strlen(info.fname) >= WAVES_NAME_BYTES) continue;
        char name[WAVES_NAME_BYTES];
        memcpy(name, info.fname, strlen(info.fname) + 1);
        unsigned i = count < USER_WAVES ? count++ : USER_WAVES;
        if (i == USER_WAVES && strcasecmp(name, out[USER_WAVES - 1]) >= 0) continue;
        if (i == USER_WAVES) i = USER_WAVES - 1;
        for (; i > 0 && strcasecmp(name, out[i - 1]) < 0; --i) memcpy(out[i], out[i - 1], WAVES_NAME_BYTES);
        memcpy(out[i], name, WAVES_NAME_BYTES);
    }
    f_closedir(&dir);
    return count;
}

unsigned waves_load(void)
{
    for (unsigned i = 0; i < USER_WAVE_POINTS; ++i) {
        cosines[i] = cosf(TWO_PI * i / USER_WAVE_POINTS);
        sines[i] = sinf(TWO_PI * i / USER_WAVE_POINTS);
    }
    static const char *const folders[2] = { WAVES_OSC_FOLDER, WAVES_TRANSIENT_FOLDER };
    unsigned loaded = 0;
    for (unsigned kind = 0; kind < 2; ++kind) {
        f_mkdir(folders[kind]);
        counts[kind] = list(folders[kind], names[kind]);
        for (unsigned n = 0; n < USER_WAVES; ++n) {
            if (kind == WAVES_OSC) user_wave_set(n, 0); else user_transient_set(n, 0);
            if (n >= counts[kind]) continue;
            char path[sizeof WAVES_TRANSIENT_FOLDER + WAVES_NAME_BYTES + 1];
            size_t folder_length = strlen(folders[kind]);
            memcpy(path, folders[kind], folder_length);
            path[folder_length] = '/';
            memcpy(path + folder_length + 1, names[kind][n], WAVES_NAME_BYTES);
            uint32_t rate = 0, frames = read_wav(path, USER_TRANSIENT_MAX, &rate);
            if (frames < 2) continue;
            if (kind == WAVES_OSC) build_wave(n, frames); else build_transient(n, frames, rate);
            loaded++;
        }
    }
    return loaded;
}

unsigned waves_count(unsigned kind) { return counts[kind]; }

const char *waves_name(unsigned kind, unsigned n) { return n < counts[kind] ? names[kind][n] : ""; }
