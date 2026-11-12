/* Front-panel input and LEDs. Buttons are active-low GPIO inputs, sampled
 * every main-loop pass with a debounce delay after each change. The encoder
 * is a quadrature pair on GPIO1 sampled at the audio block rate. DATA 2 is
 * a pot on ADC input AIN0, read at the far end as 4095 - code. */
#include "panel.h"
#include "board.h"
#include "clock.h"
#include "gpio_v2.h"
#include "tsc_adc.h"
#include "hw_types.h"
#include "hw_cm_per.h"
#include "hw_cm_wkup.h"
#include "soc_AM335x.h"

#define DEBOUNCE_MS         20u
#define HOLD_MS             750u
#define EVENT_QUEUE         16u     /* a power of two */

#define ENCODER_GPIO        SOC_GPIO_1_REGS
#define ENCODER_PIN_A       16
#define ENCODER_PIN_B       17
#define ENCODER_REST        3u      /* both inputs high between detents */
#define ENCODER_TRANSITIONS_PER_DETENT 4

#define ADC_MODULE_HZ       24000000u
#define ADC_CLOCK_HZ        3000000u
#define ADC_STEP            0u      /* STEPCONFIG1 */
#define ADC_STEP_ENABLE_BIT 1u      /* STEPENABLE bit of step 1 */
#define ADC_FULL_SCALE      4095.0f
#define ADC_CODE_MASK       0xfffu
#define DATA2_SMOOTHING     0.05f

struct pin { uint32_t gpio; uint8_t pin; };

static const struct pin buttons[PANEL_BUTTONS] = {
    { SOC_GPIO_1_REGS, 20 }, { SOC_GPIO_1_REGS, 21 }, { SOC_GPIO_1_REGS, 22 },
    { SOC_GPIO_1_REGS, 23 }, { SOC_GPIO_1_REGS, 24 }, { SOC_GPIO_1_REGS, 25 },
    { SOC_GPIO_1_REGS, 26 }, { SOC_GPIO_1_REGS, 27 },              /* channels 1-8 */
    [BUTTON_LOAD] = { SOC_GPIO_1_REGS, 28 },
    [BUTTON_SAVE] = { SOC_GPIO_1_REGS, 30 },
    [BUTTON_UTILITIES] = { SOC_GPIO_1_REGS, 31 },
    [BUTTON_CHANNELS] = { SOC_GPIO_0_REGS, 0 },
    [BUTTON_SAMPLE] = { SOC_GPIO_0_REGS, 1 },
    [BUTTON_SAMPLING_SETUP] = { SOC_GPIO_0_REGS, 30 },
    [BUTTON_ZONES] = { SOC_GPIO_2_REGS, 20 },
    [BUTTON_PITCH] = { SOC_GPIO_0_REGS, 27 },
    [BUTTON_LEVEL] = { SOC_GPIO_0_REGS, 28 },
    [BUTTON_PHASE_MOD] = { SOC_GPIO_0_REGS, 31 },
    [BUTTON_MUTATE] = { SOC_GPIO_3_REGS, 4 },
    [BUTTON_PAN_MIX] = { SOC_GPIO_0_REGS, 10 },
    [BUTTON_SAMPLE_START] = { SOC_GPIO_2_REGS, 9 },
    [BUTTON_SAMPLE_END] = { SOC_GPIO_3_REGS, 14 },
    [BUTTON_LOOP_START] = { SOC_GPIO_3_REGS, 15 },
    [BUTTON_LOOP_END] = { SOC_GPIO_3_REGS, 16 },
    [BUTTON_PLAY_MODE] = { SOC_GPIO_3_REGS, 19 },
    [BUTTON_ENVELOPE] = { SOC_GPIO_3_REGS, 20 },
    [BUTTON_LOOP_MODE] = { SOC_GPIO_3_REGS, 21 },
    [BUTTON_ENCODER] = { SOC_GPIO_1_REGS, 19 },
};

static const struct pin leds[PANEL_LEDS] = {
    { SOC_GPIO_2_REGS, 11 }, { SOC_GPIO_2_REGS, 12 }, { SOC_GPIO_2_REGS, 13 },
    { SOC_GPIO_2_REGS, 14 }, { SOC_GPIO_2_REGS, 15 }, { SOC_GPIO_2_REGS, 16 },
    { SOC_GPIO_2_REGS, 17 }, { SOC_GPIO_0_REGS, 8 },              /* channels 1-8 */
    [LED_PLAY_MODE_GATED] = { SOC_GPIO_2_REGS, 8 },
    [LED_PLAY_MODE_1_SHOT] = { SOC_GPIO_2_REGS, 7 },
    [LED_LOOP_MODE_1] = { SOC_GPIO_0_REGS, 9 },
    [LED_LOOP_MODE_2] = { SOC_GPIO_0_REGS, 11 },
    [LED_SAMPLE] = { SOC_GPIO_2_REGS, 6 },
};

/* Quadrature step for (previous state << 2 | state), clockwise positive;
 * 0 for no or invalid change */
static const int8_t quadrature[16] = { 0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0 };

static uint8_t down[PANEL_BUTTONS], held[PANEL_BUTTONS];
static uint32_t settle_ms[PANEL_BUTTONS], pressed_ms[PANEL_BUTTONS];
static struct panel_event queue[EVENT_QUEUE];
static uint32_t queue_read, queue_write;
static volatile int32_t encoder_detents;
static int32_t encoder_seen;
static uint32_t encoder_state;
static int32_t encoder_partial;
static float data2;
static uint32_t virtual_mask, virtual_until_ms;
static int virtual_steps, virtual_active;

static void post(uint8_t kind, uint8_t button, int16_t steps)
{
    if (queue_write - queue_read >= EVENT_QUEUE) return;
    queue[queue_write++ & (EVENT_QUEUE - 1)] = (struct panel_event){ kind, button, steps };
}

static void adc_init(void)
{
    module_clock_enable(SOC_CM_WKUP_REGS + CM_WKUP_ADC_TSC_CLKCTRL);
    TSCADCConfigureAFEClock(SOC_ADC_TSC_0_REGS, ADC_MODULE_HZ, ADC_CLOCK_HZ);
    TSCADCTSTransistorConfig(SOC_ADC_TSC_0_REGS, TSCADC_TRANSISTOR_DISABLE);
    TSCADCStepConfigProtectionDisable(SOC_ADC_TSC_0_REGS);
    TSCADCTSModeConfig(SOC_ADC_TSC_0_REGS, TSCADC_GENERAL_PURPOSE_MODE);
    TSCADCTSStepOperationModeControl(SOC_ADC_TSC_0_REGS, TSCADC_SINGLE_ENDED_OPER_MODE, ADC_STEP);
    TSCADCTSStepConfig(SOC_ADC_TSC_0_REGS, ADC_STEP, TSCADC_NEGATIVE_REF_ADCREFM,
                       TSCADC_POSITIVE_INP_CHANNEL1, TSCADC_NEGATIVE_INP_ADCREFM,
                       TSCADC_POSITIVE_REF_ADCREFP);
    TSCADCTSStepAnalogSupplyConfig(SOC_ADC_TSC_0_REGS, TSCADC_XPPSW_PIN_OFF, TSCADC_XNPSW_PIN_OFF,
                                   TSCADC_YPPSW_PIN_OFF, ADC_STEP);
    TSCADCTSStepAnalogGroundConfig(SOC_ADC_TSC_0_REGS, TSCADC_XNNSW_PIN_OFF, TSCADC_YPNSW_PIN_OFF,
                                   TSCADC_YNNSW_PIN_OFF, TSCADC_WPNSW_PIN_OFF, ADC_STEP);
    TSCADCTSStepFIFOSelConfig(SOC_ADC_TSC_0_REGS, ADC_STEP, TSCADC_FIFO_0);
    TSCADCTSStepModeConfig(SOC_ADC_TSC_0_REGS, ADC_STEP, TSCADC_CONTINIOUS_SOFTWARE_ENABLED);
    TSCADCTSStepAverageConfig(SOC_ADC_TSC_0_REGS, ADC_STEP, TSCADC_SIXTEEN_SAMPLES_AVG);
    TSCADCConfigureStepEnable(SOC_ADC_TSC_0_REGS, ADC_STEP_ENABLE_BIT, 1);
    TSCADCModuleStateSet(SOC_ADC_TSC_0_REGS, TSCADC_MODULE_ENABLE);
}

void panel_init(void)
{
    module_clock_enable(SOC_CM_WKUP_REGS + CM_WKUP_GPIO0_CLKCTRL);
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_GPIO1_CLKCTRL);
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_GPIO2_CLKCTRL);
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_GPIO3_CLKCTRL);
    GPIOModuleEnable(SOC_GPIO_0_REGS);
    GPIOModuleEnable(SOC_GPIO_1_REGS);
    GPIOModuleEnable(SOC_GPIO_2_REGS);
    GPIOModuleEnable(SOC_GPIO_3_REGS);
    for (unsigned b = 0; b < PANEL_BUTTONS; ++b)
        GPIODirModeSet(buttons[b].gpio, buttons[b].pin, GPIO_DIR_INPUT);
    GPIODirModeSet(ENCODER_GPIO, ENCODER_PIN_A, GPIO_DIR_INPUT);
    GPIODirModeSet(ENCODER_GPIO, ENCODER_PIN_B, GPIO_DIR_INPUT);
    for (unsigned l = 0; l < PANEL_LEDS; ++l) {
        GPIOPinWrite(leds[l].gpio, leds[l].pin, GPIO_PIN_LOW);
        GPIODirModeSet(leds[l].gpio, leds[l].pin, GPIO_DIR_OUTPUT);
    }
    encoder_state = (GPIOPinRead(ENCODER_GPIO, ENCODER_PIN_A) ? 2u : 0u) |
                    (GPIOPinRead(ENCODER_GPIO, ENCODER_PIN_B) ? 1u : 0u);
    adc_init();
}

/* Counts one detent per full quadrature cycle that ends at rest. */
void panel_encoder_sample(void)
{
    uint32_t state = (GPIOPinRead(ENCODER_GPIO, ENCODER_PIN_A) ? 2u : 0u) |
                     (GPIOPinRead(ENCODER_GPIO, ENCODER_PIN_B) ? 1u : 0u);
    encoder_partial += quadrature[encoder_state << 2 | state];
    encoder_state = state;
    if (state == ENCODER_REST) {
        if (encoder_partial >= ENCODER_TRANSITIONS_PER_DETENT) encoder_detents++;
        else if (encoder_partial <= -ENCODER_TRANSITIONS_PER_DETENT) encoder_detents--;
        encoder_partial = 0;
    }
}

static int read_button(unsigned b)
{
    if (virtual_active && (virtual_mask >> b & 1u)) return 1;
    return !GPIOPinRead(buttons[b].gpio, buttons[b].pin);
}

static void poll_virtual(uint32_t now_ms)
{
    if (!virtual_active || (int32_t)(now_ms - virtual_until_ms) < 0) return;
    /* The turn comes while the buttons are still down, as a held push-and-turn */
    if (virtual_steps) post(EVENT_TURN, 0, (int16_t)virtual_steps);
    virtual_steps = 0;
    if (virtual_mask) { virtual_mask = 0; virtual_until_ms = now_ms + DEBOUNCE_MS * 2; return; }
    virtual_active = 0;
}

static void poll_data2(void)
{
    while (TSCADCFIFOWordCountRead(SOC_ADC_TSC_0_REGS, TSCADC_FIFO_0)) {
        uint32_t code = TSCADCFIFOADCDataRead(SOC_ADC_TSC_0_REGS, TSCADC_FIFO_0) & ADC_CODE_MASK;
        float value = (ADC_FULL_SCALE - code) / ADC_FULL_SCALE;
        data2 += DATA2_SMOOTHING * (value - data2);
    }
}

struct panel_event panel_poll(uint32_t now_ms)
{
    poll_virtual(now_ms);
    poll_data2();
    for (unsigned b = 0; b < PANEL_BUTTONS; ++b) {
        if ((int32_t)(now_ms - settle_ms[b]) < 0) continue;
        int level = read_button(b);
        if (level != down[b]) {
            down[b] = level;
            settle_ms[b] = now_ms + DEBOUNCE_MS;
            if (level) { pressed_ms[b] = now_ms; held[b] = 0; post(EVENT_PRESS, b, 0); }
            else post(EVENT_RELEASE, b, held[b]);
        } else if (level && !held[b] && now_ms - pressed_ms[b] >= HOLD_MS) {
            held[b] = 1;
            post(EVENT_HOLD, b, 0);
        }
    }
    int32_t detents = encoder_detents;
    if (detents != encoder_seen) {
        post(EVENT_TURN, 0, (int16_t)(detents - encoder_seen));
        encoder_seen = detents;
    }
    if (queue_read == queue_write) return (struct panel_event){ EVENT_NONE, 0, 0 };
    return queue[queue_read++ & (EVENT_QUEUE - 1)];
}

int panel_button_down(enum panel_button button) { return down[button]; }

/* Dim is a 25% duty at about 120 Hz: on 3 of every 12 blocks */
#define DIM_PERIOD_BLOCKS   12u
#define DIM_ON_BLOCKS       3u

static volatile uint16_t dim_leds;

void panel_led(enum panel_led led, int on)
{
    dim_leds &= (uint16_t)~(1u << led);
    GPIOPinWrite(leds[led].gpio, leds[led].pin, on ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

void panel_led_dim(enum panel_led led) { dim_leds |= (uint16_t)(1u << led); }

void panel_leds_pwm(void)
{
    static unsigned phase;
    if (++phase == DIM_PERIOD_BLOCKS) phase = 0;
    if (phase != 0 && phase != DIM_ON_BLOCKS) return;
    uint32_t level = phase == 0 ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
    for (unsigned l = 0; l < PANEL_LEDS; ++l)
        if (dim_leds & (1u << l)) GPIOPinWrite(leds[l].gpio, leds[l].pin, level);
}

float panel_data2(void) { return data2; }

void panel_virtual(uint32_t mask, uint32_t hold_ms, int steps, uint32_t now_ms)
{
    virtual_mask = mask;
    virtual_steps = steps;
    virtual_until_ms = now_ms + (mask ? hold_ms : 0);
    virtual_active = 1;
}

int panel_virtual_busy(void) { return virtual_active; }
