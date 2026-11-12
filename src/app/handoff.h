/* The trial handoff record in retained DDR: the Tallent Edition sets it
 * before it chain-loads a trial build, and a reload sets it before it
 * jumps to the new build, so either starts as a trial. */
#ifndef APP_HANDOFF_H
#define APP_HANDOFF_H

#include <stdint.h>

#define HANDOFF_BASE            0x9b000000u
#define HANDOFF_TRIAL_MAGIC     0x4c415254u /* "TRAL" */
struct handoff { uint32_t magic; uint8_t build_id[8]; };
#define HANDOFF (*(volatile struct handoff *)HANDOFF_BASE)

#endif
