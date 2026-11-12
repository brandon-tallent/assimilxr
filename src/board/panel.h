/* The Assimil8or front panel: 28 buttons, 13 LEDs, the encoder and the
 * DATA 2 knob. */
#ifndef BOARD_PANEL_H
#define BOARD_PANEL_H

#include <stdint.h>

/* In the order of the remote protocol's Panel button mask. */
enum panel_button {
    BUTTON_CHANNEL_1, BUTTON_CHANNEL_8 = BUTTON_CHANNEL_1 + 7,
    BUTTON_LOAD, BUTTON_SAVE, BUTTON_UTILITIES, BUTTON_CHANNELS, BUTTON_SAMPLE,
    BUTTON_SAMPLING_SETUP, BUTTON_ZONES, BUTTON_PITCH, BUTTON_LEVEL, BUTTON_PHASE_MOD,
    BUTTON_MUTATE, BUTTON_PAN_MIX, BUTTON_SAMPLE_START, BUTTON_SAMPLE_END, BUTTON_LOOP_START,
    BUTTON_LOOP_END, BUTTON_PLAY_MODE, BUTTON_ENVELOPE, BUTTON_LOOP_MODE, BUTTON_ENCODER,
    PANEL_BUTTONS
};

enum panel_led {
    LED_CHANNEL_1, LED_CHANNEL_8 = LED_CHANNEL_1 + 7,
    LED_PLAY_MODE_GATED, LED_PLAY_MODE_1_SHOT, LED_LOOP_MODE_1, LED_LOOP_MODE_2, LED_SAMPLE,
    PANEL_LEDS
};

enum panel_event_kind { EVENT_NONE, EVENT_PRESS, EVENT_RELEASE, EVENT_HOLD, EVENT_TURN };

struct panel_event {
    uint8_t kind, button;
    int16_t steps;          /* EVENT_TURN: encoder detents, clockwise positive */
};

void panel_init(void);

/* Samples the encoder. Call at the audio block rate or faster. */
void panel_encoder_sample(void);

/* Polls buttons and DATA 2; call from the main loop with the time in ms.
 * Returns the next event, or EVENT_NONE. */
struct panel_event panel_poll(uint32_t now_ms);

int panel_button_down(enum panel_button button);
void panel_led(enum panel_led led, int on);

/* Lights an LED at the dim level until the next panel_led call */
void panel_led_dim(enum panel_led led);

/* Drives the dim LEDs; call at the audio block rate. */
void panel_leds_pwm(void);

/* DATA 2, 0 (fully counter-clockwise) to 1 */
float panel_data2(void);

/* Remote presses: the buttons in mask read as down for hold_ms; the
 * encoder turns by steps at the end of that time, before they come up.
 * Done when panel_virtual_busy() returns 0. */
void panel_virtual(uint32_t mask, uint32_t hold_ms, int steps, uint32_t now_ms);
int panel_virtual_busy(void);

#endif
