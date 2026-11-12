#ifndef PLATFORM_CLOCK_H
#define PLATFORM_CLOCK_H

/* Enable a module clock in the PRCM and wait until the module is functional.
 * clkctrl is the absolute address of its CM_*_CLKCTRL register. */
void module_clock_enable(unsigned int clkctrl);

/* Keep the interconnect and wake-up clock domains awake: a cold start
 * from the boot loader finds them as the ROM left them, and I2C0, UART0
 * and the watchdog live in them. */
void clock_domains_wake(void);

/* Busy-wait for about this many microseconds. */
void delay_us(unsigned int us);

#endif
