/* The 24 CV inputs and the 8 gate inputs. */
#ifndef BOARD_INPUTS_H
#define BOARD_INPUTS_H

#include <stdint.h>

#define INPUTS_CHANNELS     8u
#define INPUTS_COLUMNS      3u      /* A, B and C */
#define INPUTS_CV           (INPUTS_COLUMNS * INPUTS_CHANNELS)
#define INPUTS_CV_VOLTS     5.0f    /* full scale, either polarity */

/* CV input index: column * INPUTS_CHANNELS + channel */
static inline unsigned inputs_cv_index(unsigned channel, unsigned column)
{
    return column * INPUTS_CHANNELS + channel;
}

/* Starts both converter links and their DMA streams. Call after
 * audio_start, which sets up EDMA. */
void inputs_init(void);

/* The module's calibration record, 0:/cal.bin: a zero offset and a Q15 gain
 * per CV input (by CV index), then trims this firmware does not use. */
#define INPUTS_CAL_BYTES        120u
#define INPUTS_CAL_OFFSETS      0x00u
#define INPUTS_CAL_GAINS        0x30u
void inputs_calibrate(const uint8_t *record);
int inputs_calibrated(void);

/* Reads the CV inputs and gates. Call once a block in the audio interrupt. */
void inputs_sample(void);

/* The CV readings of the last inputs_sample, in volts, smoothed */
extern float inputs_cv[INPUTS_CV];

/* Gate levels, bit n for channel n + 1, and the gates that went high and
 * low since the last inputs_sample. A gate in timed rose at
 * inputs_gates_cycles[channel] (cycles_now); others at the block. A pulse
 * shorter than a block rises and falls together. */
extern uint8_t inputs_gates, inputs_gates_rising, inputs_gates_falling, inputs_gates_timed;
extern uint32_t inputs_gates_cycles[INPUTS_CHANNELS];

#endif
