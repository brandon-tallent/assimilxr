/* User wave storage (AssimiLXR); filled by src/app/waves.c in the main loop */
#include "user_waves.h"

static user_wave_table waves[USER_WAVES];
static int16_t transients[USER_WAVES][USER_TRANSIENT_MAX];
static uint8_t wave_present[USER_WAVES];
static uint32_t transient_length[USER_WAVES];

const int16_t (*user_wave(unsigned n))[USER_WAVE_POINTS]
{
    return n < USER_WAVES && wave_present[n] ? waves[n] : 0;
}

const int16_t *user_transient(unsigned n, uint32_t *length)
{
    if (n >= USER_WAVES || !transient_length[n]) return 0;
    *length = transient_length[n];
    return transients[n];
}

user_wave_table *user_wave_slot(unsigned n) { return &waves[n]; }
int16_t *user_transient_slot(unsigned n) { return transients[n]; }
void user_wave_set(unsigned n, int present) { wave_present[n] = (uint8_t)present; }
void user_transient_set(unsigned n, uint32_t length) { transient_length[n] = length; }
