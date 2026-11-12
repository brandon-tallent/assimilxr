/* The voice filters of all voices in one pass. */
#ifndef FILTER_BANK_H
#define FILTER_BANK_H

#include "Soundengine/DSPAudio/ResonantFilter.h"

/* Filters count voices' AUDIO_BLOCK-sample buffers in place, as
 * SVF_calcBlockZDF does for each. */
void filter_bank(ResonantFilter *const *filters, const uint8_t *types, int16_t *const *buffers,
                 unsigned count);

#endif
