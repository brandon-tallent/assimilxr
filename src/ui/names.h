/* Display names for LXR parameters and values */
#ifndef UI_NAMES_H
#define UI_NAMES_H

#include <stdint.h>

/* A parameter's name by its LXR name (TEXT_...) */
const char *ui_text_name(uint8_t text);
/* The full name of one of the LXR's short value names, or 0 */
const char *ui_value_name(const char *lxr);

#endif
