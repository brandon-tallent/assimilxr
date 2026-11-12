/* The panel UI: pages, editing and drawing. */
#ifndef UI_UI_H
#define UI_UI_H

#include <stdint.h>
#include "panel.h"

/* A parameter's CV route: source 0 off, 1-3 the channel's own A-C, then
 * inputs 1A-8C; amount -100 to 100 percent */
struct cv_route { int8_t source, amount; };

/* A channel's routes, by sound parameter; call ui_cv_routes_changed after
 * changing them. */
struct cv_route *ui_cv_routes(unsigned channel);
void ui_cv_routes_changed(void);

/* The folder the Channels page browses and kit CV steps through, kept with
 * the settings */
const char *ui_kits_folder(void);
void ui_set_kits_folder(const char *folder);
/* The morph amount's CV route (its own A-C are channel 1's) */
struct cv_route *ui_morph_route(void);

/* A sound parameter's range, user waves included */
void ui_value_range(uint16_t parameter, int *low, int *high);

/* The kit last loaded into every channel ("" when not from the kit
 * folders); after a remote load or save, the browsers follow */
const char *ui_kit_name(void);
void ui_kit_loaded(const char *folder, const char *name);
void ui_kits_changed(void);

void ui_init(void);
/* A start-up failure, shown under every page until the next start */
void ui_fault(const char *text);
void ui_event(struct panel_event event);
/* Reads DATA 2 and updates the channel LEDs; call every main-loop pass. */
void ui_tick(uint32_t now_ms);
/* Draws the current page into the screen image. */
void ui_draw(void);

#endif
