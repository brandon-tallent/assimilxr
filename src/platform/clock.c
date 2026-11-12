#include "clock.h"
#include "cpu.h"
#include "hw_types.h"
#include "hw_cm_per.h"
#include "hw_cm_wkup.h"
#include "soc_AM335x.h"

#define CLKTRCTRL_MASK      3u
#define CLKTRCTRL_SW_WKUP   2u


void module_clock_enable(unsigned int clkctrl)
{
    HWREG(clkctrl) = (HWREG(clkctrl) & ~CM_PER_MCASP0_CLKCTRL_MODULEMODE) |
                     CM_PER_MCASP0_CLKCTRL_MODULEMODE_ENABLE;
    while ((HWREG(clkctrl) & CM_PER_MCASP0_CLKCTRL_MODULEMODE) !=
           CM_PER_MCASP0_CLKCTRL_MODULEMODE_ENABLE) {}
    while (HWREG(clkctrl) & CM_PER_MCASP0_CLKCTRL_IDLEST) {}
}

void delay_us(unsigned int us)
{
    uint32_t start = cycles_now(), wait = us * CPU_CYCLES_PER_US;
    while (cycles_now() - start < wait) {}
}

void clock_domains_wake(void)
{
    static const unsigned int modules[] = {
        SOC_CM_PER_REGS + CM_PER_L3_CLKCTRL, SOC_CM_PER_REGS + CM_PER_L3_INSTR_CLKCTRL,
        SOC_CM_PER_REGS + CM_PER_L4LS_CLKCTRL, SOC_CM_WKUP_REGS + CM_WKUP_CONTROL_CLKCTRL,
    };
    static const unsigned int domains[] = {
        SOC_CM_PER_REGS + CM_PER_L3_CLKSTCTRL, SOC_CM_PER_REGS + CM_PER_OCPWP_L3_CLKSTCTRL,
        SOC_CM_PER_REGS + CM_PER_L3S_CLKSTCTRL, SOC_CM_PER_REGS + CM_PER_L4LS_CLKSTCTRL,
        SOC_CM_WKUP_REGS + CM_WKUP_CLKSTCTRL, SOC_CM_WKUP_REGS + CM_WKUP_CM_L3_AON_CLKSTCTRL,
    };
    for (unsigned i = 0; i < sizeof domains / sizeof domains[0]; ++i)
        HWREG(domains[i]) = (HWREG(domains[i]) & ~CLKTRCTRL_MASK) | CLKTRCTRL_SW_WKUP;
    for (unsigned i = 0; i < sizeof modules / sizeof modules[0]; ++i) module_clock_enable(modules[i]);
}
