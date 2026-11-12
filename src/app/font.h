/* Fixed-width bitmap fonts for the printable ASCII characters. */
#ifndef APP_FONT_H
#define APP_FONT_H

#include <stdint.h>

#define FONT_FIRST          32
#define FONT_LAST           126

struct font {
    uint8_t width, height, row_bytes;
    const uint8_t *glyphs;  /* per character, rows from the top, leftmost pixel in the high bit */
};

extern const struct font font_5_8, font_6_12, font_8_16, font_12_24;

#endif
