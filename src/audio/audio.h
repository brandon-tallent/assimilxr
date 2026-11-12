/* Block-based audio I/O. The render callback runs in the audio interrupt
 * once per block and must finish within the block period. Event
 * interrupts that time MIDI and gates (EVENT_IRQ_PRIORITY) may interrupt
 * it. */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_RATE              48000u
#define AUDIO_BLOCK             32u     /* frames per render call */
#define AUDIO_CHANNELS          8u      /* channel outputs */
#define AUDIO_CODEC_OVERSAMPLE  4u      /* the codec runs at 4 x AUDIO_RATE */
#define EVENT_IRQ_PRIORITY      0u      /* interrupt priorities: 0 is the highest */
#define AUDIO_IRQ_PRIORITY      1u

struct audio_input {
    float sample[2][AUDIO_BLOCK];       /* Sample inputs L and R, -1..1 */
};

struct audio_output {
    float channel[AUDIO_CHANNELS][AUDIO_BLOCK];
    float mix[2][AUDIO_BLOCK];          /* mix outputs L and R */
};

/* block_start: the cycle count (cycles_now) when the block's interrupt came */
typedef void (*audio_render_fn)(const struct audio_input *, struct audio_output *, uint32_t block_start);

struct audio_stats {
    uint32_t blocks;
    uint32_t render_cycles_last, render_cycles_min, render_cycles_max;
    uint32_t interval_last, interval_min, interval_max;
    uint32_t over_budget;
    uint32_t histogram[8];              /* render load in eighths of the budget */
    uint32_t dma_errors;
    uint32_t output_phase;              /* McASP0 read position when a block starts */
};

/* Configures the McASPs, the codec and EDMA, then starts streaming. */
int audio_start(audio_render_fn render);
void audio_stats(struct audio_stats *out, int reset);

#endif
