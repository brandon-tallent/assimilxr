/* Reload: replaces the running firmware with a new build without a
 * restart. The build arrives as a difference from the image this firmware
 * started from, which start.S keeps a copy of; the result is checked, then
 * the firmware stops its audio, DMA and interrupts and jumps to it, as the
 * Tallent Edition's chain-load does. A trial's reload runs from RAM, so a
 * reset still returns to the installed firmware; an installed build first
 * makes the new build `app`. */
#ifndef APP_RELOAD_H
#define APP_RELOAD_H

#include <stdint.h>

/* Begin: the new image's length and CRC-32. 0 if this build cannot reload. */
int reload_begin(uint32_t length, uint32_t crc);
/* Difference bytes at their offset in the stream; out of order or bad
 * data fails the reload at End. */
void reload_data(uint32_t offset, const uint8_t *data, uint32_t bytes);
/* The staged image's CRC-32 and length; 1 when it matches Begin's */
int reload_end(uint32_t *crc, uint32_t *length);
/* An installed build writes the staged image as `app` on the system card,
 * then on the front card (card is 1 when that worked too); 0 if the system
 * card's failed or none is ready */
int reload_keep(int *card);
/* Jumps to the staged image; returns only if none is ready. */
void reload_run(void);

#endif
