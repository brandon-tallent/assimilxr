/* Cortex-A8 helpers on top of CMSIS-Core. */
#ifndef PLATFORM_CPU_H
#define PLATFORM_CPU_H

#include <stdint.h>
#include "cmsis_compiler.h"

#define PMCR_ENABLE         (1u << 0)
#define PMCR_CYCLE_RESET    (1u << 2)
#define PMCNTEN_CYCLE       (1u << 31)
#define CPSR_IRQ_MASKED     (1u << 7)

/* The boot loader sets the MPU to 998.4 MHz: 665,600 cycles a 32-frame
 * block, measured against the codec's clock. */
#define CPU_HZ              998400000u
#define CPU_CYCLES_PER_US   (CPU_HZ / 1000000u)

/* The PMU cycle counter runs at the CPU clock. */
static inline void cycles_start(void)
{
    __set_CP(15, 0, PMCR_ENABLE | PMCR_CYCLE_RESET, 9, 12, 0);
    __set_CP(15, 0, PMCNTEN_CYCLE, 9, 12, 1);
}

static inline uint32_t cycles_now(void)
{
    uint32_t value;
    __get_CP(15, 0, value, 9, 13, 0);
    return value;
}

static inline uint32_t irq_save(void)
{
    uint32_t cpsr = __get_CPSR();
    __disable_irq();
    return cpsr;
}

static inline void irq_restore(uint32_t cpsr)
{
    if (!(cpsr & CPSR_IRQ_MASKED)) __enable_irq();
}

#endif
