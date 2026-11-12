#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

#include <stdint.h>

#define DISPLAY_SIDE 128

void display_init(void);
/* A frame is sent as display_begin_frame, then DISPLAY_SIDE * DISPLAY_SIDE
 * RGB565 pixels in rows from the top, in any number of display_write calls.
 * Each pixel takes about 0.6 us. */
void display_begin_frame(void);
void display_write(const uint16_t *pixels, unsigned int count);

/* Brightness 1 (dimmest) to DISPLAY_BRIGHTNESS_MAX, applied at the next frame */
#define DISPLAY_BRIGHTNESS_MAX 16u
void display_brightness(unsigned level);

#endif
