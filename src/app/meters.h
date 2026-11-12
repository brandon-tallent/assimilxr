#ifndef APP_METERS_H
#define APP_METERS_H

#include "audio.h"

#define METERS              (AUDIO_CHANNELS + 4u)   /* outputs, mix L/R, inputs L/R */

extern volatile float meters_peak[METERS];

/* Takes the peak of each output and input over one block. */
void meters_update(const struct audio_input *in, const struct audio_output *out);

#endif
