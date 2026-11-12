/* Kit changes from CV. Edges are found at the audio block rate, so a
 * trigger of a millisecond is seen; the main loop takes what they ask for. */
#include "kit_cv.h"

/* As the Assimil8or's preset CV: fires above 1.70 V, re-arms below 1.50 V */
#define FIRE_VOLTS          1.70f
#define REARM_VOLTS         1.50f
#define SELECT_FULL_VOLTS   5.0f
#define SELECT_HYSTERESIS   0.2f    /* of a kit's share of the range */
#define TRIGGER_ROLES       KIT_CV_SELECT

static uint8_t inputs[KIT_CV_ROLES] = { KIT_CV_OFF, KIT_CV_OFF, KIT_CV_OFF, KIT_CV_OFF };
static uint8_t armed[TRIGGER_ROLES];
static volatile uint32_t fired[TRIGGER_ROLES];
static uint32_t taken[TRIGGER_ROLES];
static volatile unsigned kits;
static volatile int selected = -1;

void kit_cv_set_input(enum kit_cv_role role, uint8_t input) { inputs[role] = input; }
uint8_t kit_cv_input(enum kit_cv_role role) { return inputs[role]; }
void kit_cv_set_kits(unsigned count) { kits = count; }

void kit_cv_sample(const float *cv)
{
    for (unsigned r = 0; r < TRIGGER_ROLES; ++r) {
        if (inputs[r] == KIT_CV_OFF) continue;
        float v = cv[inputs[r]];
        if (armed[r] && v > FIRE_VOLTS) { armed[r] = 0; fired[r]++; }
        else if (!armed[r] && v < REARM_VOLTS) armed[r] = 1;
    }
    unsigned count = kits;
    if (inputs[KIT_CV_SELECT] == KIT_CV_OFF || !count) { selected = -1; return; }
    float position = cv[inputs[KIT_CV_SELECT]] / SELECT_FULL_VOLTS * (float)count;
    int now = selected;
    /* Moves only past a margin around the edges of the current kit's share */
    if (now < 0 || position < now - SELECT_HYSTERESIS || position > now + 1 + SELECT_HYSTERESIS) {
        int index = (int)position;
        selected = index < 0 ? 0 : index >= (int)count ? (int)count - 1 : index;
    }
}

int kit_cv_poll(int current, unsigned count)
{
    if (!count) return -1;
    int target = current;
    for (unsigned r = 0; r < TRIGGER_ROLES; ++r) {
        uint32_t f = fired[r];
        if (f == taken[r]) continue;
        uint32_t n = f - taken[r];
        taken[r] = f;
        if (r == KIT_CV_FIRST) target = 0;
        else if (r == KIT_CV_NEXT) target = target < 0 ? (int)n - 1 : target + (int)n;
        else target = target < 0 ? 0 : target - (int)n;
    }
    static int last_selected = -1;
    int s = selected;
    if (s != last_selected) {
        last_selected = s;
        if (s >= 0) target = s;
    }
    /* No wrap, as the Assimil8or's preset stepping */
    if (target >= (int)count) target = (int)count - 1;
    if (target < 0) target = current < 0 ? -1 : 0;
    return target == current ? -1 : target;
}
