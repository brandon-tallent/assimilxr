#ifndef APP_REMOTE_H
#define APP_REMOTE_H

#include <stdint.h>

enum { BOOT_STABLE, BOOT_TRIAL };

struct identity { uint32_t magic[2]; uint32_t protocol; uint8_t build_id[8]; uint32_t image_crc; };
extern const struct identity identity;
#define REMOTE_MARKER       "ASSIMILXR"
#define REMOTE_MARKER_BYTES 9u
extern const char identity_marker[REMOTE_MARKER_BYTES] __attribute__((nonstring));

/* The diagnostics reply, in the patch protocol's layout. */
struct remote_counters {
    uint32_t uptime_ms, main_loops, foreground_polls, rx_bytes, rx_sysex_frames,
             rx_sysex_rejects, rx_sysex_overflows, rx_stray_data, tx_sysex_bytes,
             tx_other_bytes, tx_frame_drops, render_calls, render_cycles_last,
             render_cycles_min, render_cycles_max, render_interval_last,
             render_interval_min, render_interval_max, render_over_budget,
             render_histogram[8], watchdog_kicks, heap_top, reset_status, status;
};
_Static_assert(sizeof(struct remote_counters) == 124, "diagnostics wire layout");

/* Fills in the fields this module does not own; reset clears timing. */
typedef void (*remote_diagnostics_fn)(struct remote_counters *, int reset);

/* Receives each MIDI channel message (data2 0 for one-byte messages) and
 * each realtime byte (data 0), with its arrival time (cycles_now). Called
 * from the MIDI receive interrupt. */
typedef void (*remote_channel_fn)(uint8_t status, uint8_t data1, uint8_t data2, uint32_t cycles);

void remote_init(uint32_t boot_mode, remote_diagnostics_fn diagnostics, remote_channel_fn channel);
void remote_poll(uint32_t now_ms);
/* Restarts the module once pending replies are sent. While this firmware
 * runs as a trial, the restart boots the installed firmware. */
void remote_reboot(void);
/* Restarts the module at once */
void remote_reset_now(void);
/* BOOT_STABLE or BOOT_TRIAL; a promoted trial is stable */
uint32_t remote_boot_mode(void);

#endif
