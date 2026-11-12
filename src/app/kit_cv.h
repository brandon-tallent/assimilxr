/* Kit changes from CV, as the Assimil8or's Inc/Dec Preset CV: inputs that
 * step to the next or previous kit or back to the first on a rising edge,
 * and one whose voltage picks the kit. The kits are those of the Channels
 * page's browser. */
#ifndef APP_KIT_CV_H
#define APP_KIT_CV_H

#include <stdint.h>

enum kit_cv_role { KIT_CV_NEXT, KIT_CV_PREVIOUS, KIT_CV_FIRST, KIT_CV_SELECT, KIT_CV_ROLES };
#define KIT_CV_OFF          0xffu   /* no input; otherwise an inputs_cv index */

void kit_cv_set_input(enum kit_cv_role role, uint8_t input);
uint8_t kit_cv_input(enum kit_cv_role role);
/* How many kits Select spreads 0-5 V over */
void kit_cv_set_kits(unsigned count);

/* Audio interrupt: reads the inputs once a block */
void kit_cv_sample(const float *cv);

/* Main loop: the kit the CV asks for, from the current one, or -1 for no
 * change */
int kit_cv_poll(int current, unsigned count);

#endif
