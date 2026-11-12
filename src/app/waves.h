/* User waves from the front card: AssimiLXR/osc (single cycles) and
 * AssimiLXR/transients (one-shots). Main loop only. */
#ifndef APP_WAVES_H
#define APP_WAVES_H

#define WAVES_NAME_BYTES    40u
enum { WAVES_OSC, WAVES_TRANSIENT };

/* Reads both folders (creating them when missing) into the engine's user
 * slots. Returns the number of files loaded. */
unsigned waves_load(void);
unsigned waves_count(unsigned kind);
const char *waves_name(unsigned kind, unsigned n);

#endif
