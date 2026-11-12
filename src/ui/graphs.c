/* Pictures of the open section: the oscillator waveform, the envelope
 * shapes (stepped through the engine's own envelope code) and the filter
 * response (from the coefficients the voice uses). */
#include "graphs.h"
#include "screen.h"
#include "Soundengine/DSPAudio/SlopeEg2.h"
#include "Soundengine/DSPAudio/Decay.h"
#include "Soundengine/DSPAudio/Oscillator.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/MIDI/valueShaper.h"
#include "Soundengine/globals.h"
#include <math.h>
#include <stdio.h>

#define LINE                RGB565(255, 80, 24)
#define AREA                RGB565(64, 20, 6)
#define GRID                RGB565(48, 48, 48)
#define MAX_WIDTH           128
#define PI                  3.14159265f
#define PWM_CENTRE          0.5f
#define ENVELOPE_BLOCKS     4000    /* long enough for the slowest settings */
#define ENVELOPE_END        0.001
#define FILTER_LOW_HZ       20.0f
#define FILTER_HIGH_HZ      20000.0f
#define FILTER_TOP_DB       18.0f
#define FILTER_BOTTOM_DB    -36.0f
#define MIDI_FULL           127.0f
#define GRID_DB_STEP        12.0f
#define LABEL               RGB565(190, 190, 190)
#define MS_PER_SECOND       1000u

/* Draws values (0 at the bottom, 1 at the top) as a line over an area
 * filled to the base (0 the bottom, 0.5 the middle). */
static void plot_from(const float *v, int count, int x, int y, int w, int h, float base)
{
    int previous = -1, by = y + h - 1 - (int)(base * (h - 1) + 0.5f);
    for (int i = 0; i < count && i < w; ++i) {
        float clamped = v[i] < 0 ? 0 : v[i] > 1 ? 1 : v[i];
        int py = y + h - 1 - (int)(clamped * (h - 1) + 0.5f);
        if (py <= by) screen_fill(x + i, py, 1, by - py + 1, AREA);
        else screen_fill(x + i, by, 1, py - by + 1, AREA);
        int top = previous < 0 ? py : previous < py ? previous : py;
        int bottom = previous < 0 ? py : previous > py ? previous : py;
        screen_fill(x + i, top, 1, bottom - top + 1, LINE);
        previous = py;
    }
}

static void plot(const float *v, int count, int x, int y, int w, int h) { plot_from(v, count, x, y, w, h, 0); }

void graph_wave(int waveform, int pwm, int x, int y, int w, int h)
{
    float v[MAX_WIDTH];
    float width = PWM_CENTRE + (pwm / MIDI_FULL) * PWM_CENTRE * 0.9f;
    uint32_t noise = 1;
    for (int i = 0; i < w; ++i) {
        float t = (float)i / w;
        switch (waveform) {
        case SINE: v[i] = 0.5f + 0.5f * sinf(2 * PI * t); break;
        case TRI: v[i] = t < 0.5f ? 2 * t : 2 - 2 * t; break;
        case SAW: v[i] = 1 - t; break;
        case REC: v[i] = t < 0.5f ? 1 : 0; break;
        case PWM: v[i] = t < width ? 1 : 0; break;
        default: noise = noise * 1103515245u + 12345u; v[i] = (noise >> 16 & 0xff) / 255.0f; break;
        }
    }
    plot_from(v, w, x, y, w, h, 0.5f);
}

static unsigned blocks_ms(unsigned blocks)
{
    return (unsigned)((uint64_t)blocks * OUTPUT_DMA_SIZE * MS_PER_SECOND / (uint32_t)REAL_FS);
}

/* Draws a block-rate envelope over the width, labelled with its length. */
static void draw_steps(float *steps, int count, int x, int y, int w, int h)
{
    float v[MAX_WIDTH];
    for (int i = 0; i < w; ++i) v[i] = steps[(long)i * (count - 1) / (w - 1)];
    plot(v, w, x, y, w, h);
    char label[12];
    unsigned ms = blocks_ms((unsigned)count);
    if (ms >= MS_PER_SECOND) snprintf(label, sizeof label, "%u.%02u s", ms / MS_PER_SECOND, ms % MS_PER_SECOND / 10);
    else snprintf(label, sizeof label, "%u ms", ms);
    screen_text(&font_5_8, x + w - screen_text_width(&font_5_8, label), y, label, LABEL);
}

void graph_envelope(int attack, int decay, int slope, int x, int y, int w, int h)
{
    static float steps[ENVELOPE_BLOCKS];
    SlopeEg2 eg;
    slopeEg2_init(&eg);
    slopeEg2_setAttack(&eg, (uint8_t)attack, 0);
    slopeEg2_setDecay(&eg, (uint8_t)decay, 0);
    slopeEg2_setSlope(&eg, (uint8_t)slope);
    slopeEg2_trigger(&eg);
    int count = 0;
    do steps[count++] = (float)slopeEg2_calc(&eg);
    while (count < ENVELOPE_BLOCKS && (eg.state != EG_D || eg.value > ENVELOPE_END));
    draw_steps(steps, count, x, y, w, h);
}

void graph_decay(int decay, int slope, int x, int y, int w, int h)
{
    static float steps[ENVELOPE_BLOCKS];
    DecayEg eg;
    DecayEg_init(&eg);
    DecayEg_setDecay(&eg, (uint8_t)decay);
    DecayEg_setSlope(&eg, (uint8_t)slope);
    DecayEg_trigger(&eg);
    int count = 0;
    do steps[count++] = DecayEg_calc(&eg);
    while (count < ENVELOPE_BLOCKS && eg.value > ENVELOPE_END);
    draw_steps(steps, count, x, y, w, h);
}

void graph_filter(int type, int cutoff, int resonance, int x, int y, int w, int h)
{
    ResonantFilter f;
    SVF_init(&f);
    SVF_directSetFilterValue(&f, valueShaperF2F(cutoff / MIDI_FULL, FILTER_SHAPER));
    SVF_setReso(&f, resonance / MIDI_FULL);
    float r = f.q;
    float fc = f.f * REAL_FS;   /* f is the cutoff over the sample rate */
    float v[MAX_WIDTH];
    for (int i = 0; i < w; ++i) {
        float hz = FILTER_LOW_HZ * powf(FILTER_HIGH_HZ / FILTER_LOW_HZ, (float)i / (w - 1));
        float k = hz / fc, re = 1 - k * k, im = 2 * r * k, denominator = re * re + im * im;
        float num;  /* |numerator|^2 */
        switch (type) {
        case FILTER_LP: num = 1; break;
        case FILTER_HP: num = k * k * k * k; break;
        case FILTER_BP: num = k * k; break;
        case FILTER_UNITY_BP: num = im * im; break;
        case FILTER_NOTCH: num = re * re; break;
        case FILTER_PEAK: num = (1 + k * k) * (1 + k * k); break;
        default: num = denominator; break;
        }
        float db = 10 * log10f(num / denominator + 1e-9f);
        v[i] = (db - FILTER_BOTTOM_DB) / (FILTER_TOP_DB - FILTER_BOTTOM_DB);
    }
    for (float db = FILTER_BOTTOM_DB + GRID_DB_STEP; db < FILTER_TOP_DB; db += GRID_DB_STEP) {
        int gy = y + h - 1 - (int)((db - FILTER_BOTTOM_DB) / (FILTER_TOP_DB - FILTER_BOTTOM_DB) * (h - 1));
        for (int i = 0; i < w; i += 2) screen_fill(x + i, gy, 1, 1, GRID);
    }
    plot(v, w, x, y, w, h);
}

/* The amp envelope's attack time, and its decay time to -60 dB */
unsigned graph_envelope_ms(int attack, int decay, int slope, int decay_part)
{
    SlopeEg2 eg;
    slopeEg2_init(&eg);
    slopeEg2_setAttack(&eg, (uint8_t)attack, 0);
    slopeEg2_setDecay(&eg, (uint8_t)decay, 0);
    slopeEg2_setSlope(&eg, (uint8_t)slope);
    slopeEg2_trigger(&eg);
    unsigned attack_blocks = 0, decay_blocks = 0;
    for (unsigned i = 0; i < ENVELOPE_BLOCKS; ++i) {
        int in_attack = eg.state == EG_A;
        float v = slopeEg2_calc(&eg);
        if (in_attack) ++attack_blocks;
        else if (v > ENVELOPE_END) ++decay_blocks;
        else break;
    }
    return blocks_ms(decay_part ? decay_blocks : attack_blocks);
}

/* The pitch envelope's time to -60 dB */
unsigned graph_decay_ms(int decay, int slope)
{
    DecayEg eg;
    DecayEg_init(&eg);
    DecayEg_setDecay(&eg, (uint8_t)decay);
    DecayEg_setSlope(&eg, (uint8_t)slope);
    DecayEg_trigger(&eg);
    unsigned blocks = 0;
    while (blocks < ENVELOPE_BLOCKS && DecayEg_calc(&eg) > ENVELOPE_END) ++blocks;
    return blocks_ms(blocks);
}

/* The filter's cutoff in Hz for its 0-127 value */
float graph_cutoff_hz(int cutoff)
{
    ResonantFilter f;
    SVF_init(&f);
    SVF_directSetFilterValue(&f, valueShaperF2F(cutoff / MIDI_FULL, FILTER_SHAPER));
    return f.f * REAL_FS;
}
