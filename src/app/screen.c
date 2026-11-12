/* Drawing into the display image and sending it to the panel. */
#include "screen.h"
#include <string.h>

#define ROWS_PER_STEP       8

static uint16_t frame[SCREEN_PIXELS], sent[SCREEN_PIXELS];
static unsigned next_row = DISPLAY_SIDE;

void screen_clear(void) { memset(frame, 0, sizeof frame); }

void screen_fill(int x, int y, int w, int h, uint16_t colour)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_SIDE) w = DISPLAY_SIDE - x;
    if (y + h > DISPLAY_SIDE) h = DISPLAY_SIDE - y;
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) frame[j * DISPLAY_SIDE + i] = colour;
}

int screen_text(const struct font *f, int x, int y, const char *s, uint16_t colour)
{
    for (; *s; ++s, x += f->width) {
        unsigned c = (unsigned char)*s;
        if (c < FONT_FIRST || c > FONT_LAST) c = '?';
        const uint8_t *g = f->glyphs + (c - FONT_FIRST) * f->height * f->row_bytes;
        for (int row = 0; row < f->height; ++row, g += f->row_bytes) {
            int py = y + row;
            if (py < 0 || py >= DISPLAY_SIDE) continue;
            for (int col = 0; col < f->width; ++col) {
                int px = x + col;
                if (px >= 0 && px < DISPLAY_SIDE && (g[col >> 3] & (0x80u >> (col & 7))))
                    frame[py * DISPLAY_SIDE + px] = colour;
            }
        }
    }
    return x;
}

int screen_text_width(const struct font *f, const char *s) { return (int)strlen(s) * f->width; }

int screen_idle(void) { return next_row >= DISPLAY_SIDE; }

void screen_present(void)
{
    memcpy(sent, frame, sizeof sent);
    display_begin_frame();
    next_row = 0;
}

void screen_step(void)
{
    if (screen_idle()) return;
    display_write(sent + next_row * DISPLAY_SIDE, ROWS_PER_STEP * DISPLAY_SIDE);
    next_row += ROWS_PER_STEP;
}

const uint16_t *screen_pixels(void) { return sent; }
