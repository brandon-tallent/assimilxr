/* MIDI settings, kept on the front card: the LXR-02's own glo.cfg (so an
 * LXR-02 owner's file works as it is) and SETTINGS_FILE for the channels
 * the LXR-02 has no setting for. */
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "engine.h"

/* Reads the files into the engine; missing files leave the defaults. */
void settings_load(void);
/* Writes the engine's setup. Returns 1 when saved. */
int settings_save(void);
/* Loads the kit last loaded into all channels, as saved; after the waves */
void settings_load_kit(void);

/* Display brightness 1-DISPLAY_BRIGHTNESS_MAX, kept with the settings */
unsigned settings_brightness(void);
void settings_set_brightness(unsigned level);

/* The Sample inputs' levels, 0-SETTINGS_INPUT_MAX, kept with the settings */
enum { SETTINGS_INPUT_SEND, SETTINGS_INPUT_DRY };
#define SETTINGS_INPUT_MAX  127u
uint8_t settings_input_level(unsigned which);
void settings_set_input_level(unsigned which, uint8_t level);

#endif
