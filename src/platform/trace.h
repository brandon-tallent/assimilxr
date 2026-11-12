/* A progress and fault record in DDR that survives a watchdog reset, so the
 * stable firmware can read it after a failed trial (Peek 0x9b0000c0). */
#ifndef PLATFORM_TRACE_H
#define PLATFORM_TRACE_H

#include <stdint.h>

#define TRACE_ADDRESS   0x9b0000c0u
#define TRACE_MAGIC     0x5452584cu /* "LXRT" */

enum trace_stage {
    STAGE_RESET = 1, STAGE_MEMORY, STAGE_INTERRUPTS, STAGE_PINS, STAGE_REMOTE, STAGE_DISPLAY, STAGE_ENGINE,
    STAGE_EDMA, STAGE_MCASP, STAGE_CODEC, STAGE_AUDIO_STARTED, STAGE_INPUTS, STAGE_STORAGE, STAGE_RESTORE,
    STAGE_MAIN_LOOP,
};

enum trace_fault { FAULT_NONE, FAULT_DATA_ABORT, FAULT_PREFETCH_ABORT, FAULT_UNDEFINED };

struct trace {
    uint32_t magic, stage, main_loops, audio_blocks;
    uint32_t fault, fault_pc, fault_address, fault_status;   /* written by start.S at offset 16 */
    uint32_t codec_result, screen_frames, last_command, data_aborts_skipped;
};

#define TRACE (*(volatile struct trace *)TRACE_ADDRESS)

static inline void trace_stage(enum trace_stage stage) { TRACE.stage = stage; }

#endif
