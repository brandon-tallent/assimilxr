#include "audio.h"
#include "board.h"
#include "codec.h"
#include "cpu.h"
#include "display.h"
#include "engine.h"
#include "inputs.h"
#include "storage.h"
#include "settings.h"
#include "waves.h"
#include "install.h"
#include "memory.h"
#include "meters.h"
#include "remote.h"
#include "screen.h"
#include "panel.h"
#include "ui.h"
#include "trace.h"
#include "interrupt.h"
#include "watchdog.h"
#include "kit_cv.h"
#include "handoff.h"
#include "clock.h"
#include "hw_types.h"
#include "hw_cm_wkup.h"
#include "soc_AM335x.h"
#include <stdio.h>
#include <string.h>


#define PRM_DEVICE_RSTST        (SOC_PRM_DEVICE_REGS + 0x08u)
#define SCREEN_PERIOD_MS        40u     /* 25 frames a second */
#define WATCHDOG_PERIOD_MS      500u
#define WATCHDOG_CLOCK_HZ       32768u
#define WATCHDOG_SECONDS        20u
#define CYCLES_PER_MS           (CPU_HZ / 1000u)
#define PERCENT                 100u
#define UNMUTE_AFTER_BLOCKS     64u /* about 43 ms of rendered audio */
#define STATUS_VARIANT_SHIFT    8
#define STATUS_AUDIO_RUNNING    (1u << 9)
#define STATUS_DRIVES_SHIFT     10  /* a bit per mounted drive */
#define STATUS_CALIBRATED       (1u << 12)
#define STATUS_DMA_ERRORS_SHIFT 16
#define STATUS_DMA_ERRORS_MAX   0xffffu

static uint32_t boot_mode, reset_status, codec_error, audio_running, drives, calibrated;
static uint32_t main_loops, watchdog_kicks;
static uint64_t uptime_cycles;

void *sbrk_top(void);

static void render(const struct audio_input *in, struct audio_output *out, uint32_t block_start)
{
    panel_encoder_sample();
    panel_leds_pwm();
    inputs_sample();
    kit_cv_sample(inputs_cv);
    engine_inputs(inputs_cv, inputs_gates_rising, inputs_gates_timed, inputs_gates_cycles,
                  inputs_gates_falling);
    engine_render(in, out, block_start);
    meters_update(in, out);
}

static void diagnostics(struct remote_counters *c, int reset)
{
    struct audio_stats s;
    audio_stats(&s, reset);
    if (reset) engine_profile_reset();
    c->uptime_ms = (uint32_t)(uptime_cycles / CYCLES_PER_MS);
    c->main_loops = main_loops;
    c->render_calls = s.blocks;
    c->render_cycles_last = s.render_cycles_last;
    c->render_cycles_min = s.render_cycles_min;
    c->render_cycles_max = s.render_cycles_max;
    c->render_interval_last = s.interval_last;
    c->render_interval_min = s.interval_min;
    c->render_interval_max = s.interval_max;
    c->render_over_budget = s.over_budget;
    memcpy(c->render_histogram, s.histogram, sizeof s.histogram);
    c->watchdog_kicks = watchdog_kicks;
    c->heap_top = (uint32_t)sbrk_top();
    c->reset_status = reset_status;
    uint32_t errors = s.dma_errors < STATUS_DMA_ERRORS_MAX ? s.dma_errors : STATUS_DMA_ERRORS_MAX;
    c->status = codec_error | (uint32_t)codec_variant() << STATUS_VARIANT_SHIFT |
                (audio_running ? STATUS_AUDIO_RUNNING : 0) | drives << STATUS_DRIVES_SHIFT |
                (calibrated ? STATUS_CALIBRATED : 0) | errors << STATUS_DMA_ERRORS_SHIFT;
}

static void detect_boot(void)
{
    if (HANDOFF.magic == HANDOFF_TRIAL_MAGIC &&
        !memcmp((const void *)HANDOFF.build_id, identity.build_id, sizeof identity.build_id))
        boot_mode = BOOT_TRIAL;
    HANDOFF.magic = 0;
    reset_status = HWREG(PRM_DEVICE_RSTST);
    /* Causes stay latched until power-off: clear them so the next start
     * sees only its own */
    HWREG(PRM_DEVICE_RSTST) = reset_status;
}

/* An installed build starts the watchdog itself; a trial's was started by
 * the firmware that launched it */
static void watchdog_start(void)
{
    module_clock_enable(SOC_CM_WKUP_REGS + CM_WKUP_WDT1_CLKCTRL);
    WatchdogTimerDisable(SOC_WDT_1_REGS);
    WatchdogTimerPreScalerClkDisable(SOC_WDT_1_REGS);
    WatchdogTimerReloadSet(SOC_WDT_1_REGS, 0u - WATCHDOG_CLOCK_HZ * WATCHDOG_SECONDS);
    WatchdogTimerCounterSet(SOC_WDT_1_REGS, 0u - WATCHDOG_CLOCK_HZ * WATCHDOG_SECONDS);
    WatchdogTimerEnable(SOC_WDT_1_REGS);
}

int main(void)
{
    detect_boot();
    memset((void *)&TRACE, 0, sizeof TRACE);
    TRACE.magic = TRACE_MAGIC;
    trace_stage(STAGE_RESET);
    cycles_start();
    memory_init();
    trace_stage(STAGE_MEMORY);
    IntAINTCInit();
    trace_stage(STAGE_INTERRUPTS);
    clock_domains_wake();
    board_pins_init();
    trace_stage(STAGE_PINS);
    remote_init(boot_mode, diagnostics, engine_midi);
    trace_stage(STAGE_REMOTE);
    display_init();
    panel_init();
    trace_stage(STAGE_DISPLAY);
    ui_init();

    engine_init();
    trace_stage(STAGE_ENGINE);
    codec_error = audio_start(render);
    TRACE.codec_result = codec_error | (uint32_t)codec_variant() << STATUS_VARIANT_SHIFT;
    audio_running = !codec_error;
    inputs_init();
    trace_stage(STAGE_INPUTS);
    drives = storage_init();
    trace_stage(STAGE_STORAGE);
    if (!install_boot(boot_mode, reset_status)) ui_fault("Watchdog: restore failed");
    else if (codec_error) {
        char text[32];
        snprintf(text, sizeof text, "Audio failed: codec %lu", (unsigned long)codec_error);
        ui_fault(text);
    } else if (!(drives & 1u)) ui_fault("System card not mounted");
    static uint8_t calibration[INPUTS_CAL_BYTES];
    calibrated = storage_read("0:/cal.bin", calibration, sizeof calibration) == (int)sizeof calibration;
    if (calibrated) inputs_calibrate(calibration);
    settings_load();
    waves_load();
    settings_load_kit();
    /* Started before the interrupts, so a start that stops in a service
     * routine restarts; a trial runs on the launching firmware's watchdog */
    if (boot_mode == BOOT_STABLE) watchdog_start();
    IntMasterIRQEnable();
    trace_stage(STAGE_MAIN_LOOP);

    uint32_t last = cycles_now(), screen_due = 0, watchdog_due = 0, kicked_blocks = 0;
    int mix_enabled = 0;
    uint64_t now_ms = 0;
    for (;;) {
        uint32_t t = cycles_now();
        uptime_cycles += t - last;
        last = t;
        now_ms = uptime_cycles / CYCLES_PER_MS;
        main_loops++;
        TRACE.main_loops = main_loops;
        remote_poll((uint32_t)now_ms);
        screen_step();
        for (struct panel_event e = panel_poll((uint32_t)now_ms); e.kind != EVENT_NONE;
             e = panel_poll((uint32_t)now_ms)) ui_event(e);
        ui_tick((uint32_t)now_ms);

        struct audio_stats s;
        audio_stats(&s, 0);
        if (!mix_enabled && audio_running && s.blocks > UNMUTE_AFTER_BLOCKS) {
            codec_mix_enable(1);
            mix_enabled = 1;
        }
        if (now_ms >= screen_due && screen_idle()) {
            screen_due = now_ms + SCREEN_PERIOD_MS;
            ui_draw();
            screen_present();
            TRACE.screen_frames++;
        }
        /* Feed the watchdog only while audio blocks keep arriving. */
        if (now_ms >= watchdog_due && s.blocks != kicked_blocks) {
            watchdog_due = now_ms + WATCHDOG_PERIOD_MS;
            kicked_blocks = s.blocks;
            WatchdogTimerTriggerSet(SOC_WDT_1_REGS, watchdog_kicks++);
        }
    }
}
