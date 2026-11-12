/* User waves: single-cycle oscillator waves and one-shot transients from
 * the card, after the LXR's own (AssimiLXR). Each oscillator wave is
 * USER_WAVE_TABLES band-limited tables of USER_WAVE_POINTS, one per MIDI
 * octave, as the LXR's saw, triangle and square tables. */
#ifndef USER_WAVES_H
#define USER_WAVES_H

#include <stdint.h>

#define USER_WAVES          16u
#define USER_WAVE_TABLES    11u
#define USER_WAVE_POINTS    1024u
#define USER_TRANSIENT_MAX  96000u  /* samples at 48 kHz: 2 s */

typedef int16_t user_wave_table[USER_WAVE_TABLES][USER_WAVE_POINTS];

/* The tables of user oscillator wave n, or 0 if there is none */
const int16_t (*user_wave(unsigned n))[USER_WAVE_POINTS];
/* User transient n and its length in samples, or 0 */
const int16_t *user_transient(unsigned n, uint32_t *length);

/* For the loader: where wave or transient n goes, and marking it present */
user_wave_table *user_wave_slot(unsigned n);
int16_t *user_transient_slot(unsigned n);
void user_wave_set(unsigned n, int present);
void user_transient_set(unsigned n, uint32_t length);

#endif
