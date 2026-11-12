/* The parts of the LXR firmware outside the sound engine that the engine
 * sources call: sequencer, trigger outputs, front panel, faders, menu. */
#ifndef LXR_SHELL_H
#define LXR_SHELL_H

#include "lxr_port.h"

/* Sequencer */
#define SEQ_DEFAULT_NOTE 60     /* the note that plays a voice at its set pitch */

/* The front panel's status byte for parameters above 127 */
#define CC_2 0xd6

/* The edit buffer: the current kit's parameter values */
/* MIDI velocity sets every voice's level, whatever its kit says */
extern volatile uint8_t lxr_velocity_always;
extern uint8_t *activeParameters;    /* the selected channel's */

/* Sequencer tempo, for LFO sync */
float seq_getBpm(void);

/* Mutes, kept by src/engine/engine.c */
void seq_setMute(uint8_t voice, uint8_t isMuted);

/* Front panel faders: none, full level */
static inline float adc_getFaderVolume(uint8_t voice) { (void)voice; return 1.0f; }

#endif
