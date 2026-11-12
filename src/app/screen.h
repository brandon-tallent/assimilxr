/* The display image: drawn in RAM, then sent to the panel a few rows per
 * main-loop pass so MIDI keeps flowing. */
#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include <stdint.h>
#include "display.h"
#include "font.h"

#define SCREEN_PIXELS       (DISPLAY_SIDE * DISPLAY_SIDE)
#define RGB565(r, g, b)     ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3))

void screen_clear(void);
void screen_fill(int x, int y, int w, int h, uint16_t colour);
/* Draws s from (x, y), the top left of its first character; returns the x after it. */
int screen_text(const struct font *f, int x, int y, const char *s, uint16_t colour);
int screen_text_width(const struct font *f, const char *s);
/* Starts sending the drawn image; screen_step sends the next rows. */
void screen_present(void);
void screen_step(void);
int screen_idle(void);

/* The last image sent, for screenshots */
const uint16_t *screen_pixels(void);

#endif
