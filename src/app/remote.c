/* The MIDI port: the Tallent Edition's SysEx remote protocol (Identify,
 * Diagnostics, Peek, Poke, Reboot, Panel, Screenshot, card writes and
 * reload), and channel messages passed on to the engine.
 *
 * The receive interrupt takes each byte as it arrives: channel and realtime
 * messages go to the engine with their arrival time, and SysEx waits in rx
 * for the main loop. */
#include "remote.h"
#include "editor.h"
#include "storage.h"
#include "install.h"
#include "reload.h"
#include "board.h"
#include "panel.h"
#include "screen.h"
#include "trace.h"
#include "cpu.h"
#include "audio.h"
#include "interrupt.h"
#include "uart_irda_cir.h"
#include "hw_types.h"
#include "hw_uart_irda_cir.h"
#include "hw_cm_wkup.h"
#include "soc_AM335x.h"
#include "clock.h"
#include <string.h>

#define PROTOCOL_VERSION        1u
#define SYSEX_START             0xf0u
#define SYSEX_END               0xf7u
#define MIDI_STATUS_BIT         0x80u
#define MIDI_REALTIME_FIRST     0xf8u
#define MIDI_SYSTEM_FIRST       0xf0u
#define MIDI_TYPE_MASK          0xf0u
#define MIDI_PROGRAM_CHANGE     0xc0u
#define MIDI_CHANNEL_PRESSURE   0xd0u
#define SYSEX_MANUFACTURER      0x7du /* noncommercial */
#define SYSEX_DEVICE_FIRST      'A'
#define SYSEX_DEVICE_SECOND     '8'
#define SYSEX_HEADER_BYTES      7u
#define SYSEX_REPLY_FLAG        0x40u
#define SYSEX_GROUP             7u
#define REQUEST_BYTES           48u
#define REPLY_BYTES             126u
#define PEEK_MAX_BYTES          96u
#define CONFIRM_WORD            0x38415153u /* "SQA8" */
#define UART_FIFO_DEPTH         64u
#define TX_BYTES                2048u
#define RX_FRAME_BYTES          64u
#define RX_BYTES                2048u   /* a power of two */
#define RX_TRIGGER_BYTES        1u      /* an interrupt for every byte */
#define MIDI_DIVISOR            100u    /* the MIDI rate in 16x mode, 8N1 */

enum { CMD_IDENTIFY = 1, CMD_DIAGNOSTICS = 2, CMD_PEEK = 3, CMD_POKE = 4, CMD_FOREGROUND = 7,
       CMD_PANEL = 9, CMD_SCREENSHOT = 10, CMD_PROMOTE = 23, CMD_RESTORE = 24, CMD_REBOOT = 25,
       CMD_FILE_BEGIN = 45, CMD_FILE_DATA = 46, CMD_FILE_END = 47,
       CMD_RELOAD_BEGIN = 48, CMD_RELOAD_DATA = 49, CMD_RELOAD_END = 50, CMD_RELOAD_RUN = 51 };
#define FILE_OFFSET_BYTES       4u
#define PANEL_REQUEST_BYTES     7u      /* button mask, hold ms, encoder steps */
#define SCREENSHOT_CHUNK_REQUEST_BYTES 3u
#define SCREENSHOT_MAX_CHUNK    126u
#define PACKBITS_RUN            0x80u
#define PACKBITS_MAX            128u
#define SCREENSHOT_BYTES        (SCREEN_PIXELS * 2u + SCREEN_PIXELS / PACKBITS_MAX)
#define FOREGROUND_BYTES        8u /* storage reads, sample service state: none yet */
enum { OK, BAD_COMMAND, BAD_LENGTH, BAD_RANGE };

#define PRM_DEVICE_RSTCTRL      (SOC_PRM_DEVICE_REGS + 0x00u)
#define PRM_RSTCTRL_COLD_SW     0x02u

/* Read by the Tallent Edition's installer and by remote tools; the
 * packer fills in the build ID and CRC. */
__attribute__((section(".identity"), used))
const struct identity identity = { { 0x38415153u, 0x48435450u }, PROTOCOL_VERSION, { 0 }, 0 };

/* Right after the identity record: tells this firmware's images from the
 * stock firmware's on the card (install.c) */
__attribute__((section(".identity.marker"), used))
const char identity_marker[REMOTE_MARKER_BYTES] __attribute__((nonstring)) = REMOTE_MARKER;

volatile uint32_t data_aborts, peek_active;
static uint32_t boot_mode;
static remote_diagnostics_fn diagnostics_fn;
static remote_channel_fn channel_fn;
static uint8_t running_status, channel_data[2], in_frame;
static uint32_t channel_count;
static volatile uint8_t rx[RX_BYTES];
static volatile uint32_t rx_write, rx_read;
static struct remote_counters counters;
static uint8_t frame[RX_FRAME_BYTES];
static uint32_t frame_size, frame_active, frame_discard;
static uint8_t tx[TX_BYTES];
static uint32_t tx_read, tx_write, reboot_pending, reload_pending;
static uint32_t panel_pending, panel_sequence;
static uint8_t screenshot[SCREENSHOT_BYTES];
static uint32_t screenshot_bytes;
static uint32_t now_ms;

static uint32_t get_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put_u32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void queue(uint8_t byte) { tx[tx_write++ & (TX_BYTES - 1)] = byte; }

static void reply(uint32_t command, uint32_t sequence, uint32_t status, const uint8_t *data, uint32_t bytes)
{
    uint8_t payload[1 + REPLY_BYTES];
    payload[0] = status;
    memcpy(payload + 1, data, bytes);
    bytes++;
    uint32_t length = 9 + bytes + (bytes + SYSEX_GROUP - 1) / SYSEX_GROUP;
    if (TX_BYTES - (tx_write - tx_read) < length) { counters.tx_frame_drops++; return; }
    queue(SYSEX_START); queue(SYSEX_MANUFACTURER);
    queue(SYSEX_DEVICE_FIRST); queue(SYSEX_DEVICE_SECOND);
    queue(PROTOCOL_VERSION); queue(command | SYSEX_REPLY_FLAG);
    queue(sequence); queue(bytes);
    for (uint32_t offset = 0; offset < bytes; offset += SYSEX_GROUP) {
        uint32_t count = bytes - offset < SYSEX_GROUP ? bytes - offset : SYSEX_GROUP, high = 0;
        for (uint32_t bit = 0; bit < count; ++bit) high |= (payload[offset + bit] >> 7) << bit;
        queue(high);
        for (uint32_t bit = 0; bit < count; ++bit) queue(payload[offset + bit] & 127);
    }
    queue(SYSEX_END);
}

/* Word accesses when aligned, so peripheral registers read correctly. A
 * faulting access is skipped by the abort handler and reported. */
static int copy_checked(uint8_t *to, const uint8_t *from, uint32_t bytes)
{
    uint32_t before = data_aborts;
    peek_active = 1;
    __DSB();
    if (!(((uint32_t)to | (uint32_t)from | bytes) & 3)) {
        for (uint32_t i = 0; i < bytes; i += 4)
            *(volatile uint32_t *)(to + i) = *(const volatile uint32_t *)(from + i);
    } else {
        for (uint32_t i = 0; i < bytes; ++i) ((volatile uint8_t *)to)[i] = ((const volatile uint8_t *)from)[i];
    }
    __DSB();
    peek_active = 0;
    return data_aborts == before;
}

static void put_pixel(uint8_t *p, uint16_t pixel) { p[0] = pixel >> 8; p[1] = pixel; }

/* PackBits over pixels: a header byte n - 1 then n literal pixels, or
 * 0x80 | (n - 1) then one pixel repeated n times. */
static uint32_t encode_screenshot(const uint16_t *pixels)
{
    uint32_t out = 0, i = 0;
    while (i < SCREEN_PIXELS) {
        uint32_t run = 1;
        while (i + run < SCREEN_PIXELS && run < PACKBITS_MAX && pixels[i + run] == pixels[i]) run++;
        if (run > 1) {
            screenshot[out++] = PACKBITS_RUN | (run - 1);
            put_pixel(screenshot + out, pixels[i]);
            out += 2; i += run;
            continue;
        }
        uint32_t literal = 1;
        while (i + literal < SCREEN_PIXELS && literal < PACKBITS_MAX &&
               (i + literal + 1 >= SCREEN_PIXELS || pixels[i + literal] != pixels[i + literal + 1])) literal++;
        screenshot[out++] = literal - 1;
        for (uint32_t k = 0; k < literal; ++k, out += 2) put_pixel(screenshot + out, pixels[i + k]);
        i += literal;
    }
    return out;
}

static void handle(uint32_t command, uint32_t sequence, const uint8_t *p, uint32_t bytes)
{
    uint8_t out[REPLY_BYTES];
    uint32_t status = OK, count = 0;
    switch (command) {
    case CMD_IDENTIFY:
        if (bytes) { status = BAD_LENGTH; break; }
        memcpy(out, &identity, sizeof identity);
        put_u32(out + 24, boot_mode);
        put_u32(out + 28, 0);
        count = 32;
        break;
    case CMD_DIAGNOSTICS: {
        if (bytes > 1 || (bytes && p[0] != 1)) { status = BAD_LENGTH; break; }
        struct remote_counters snapshot = counters;
        diagnostics_fn(&snapshot, bytes);
        memcpy(out, &snapshot, sizeof snapshot);
        count = sizeof snapshot;
        break;
    }
    case CMD_FOREGROUND:
        if (bytes) { status = BAD_LENGTH; break; }
        memset(out, 0, FOREGROUND_BYTES);
        count = FOREGROUND_BYTES;
        break;
    case CMD_PEEK:
        if (bytes != 5) { status = BAD_LENGTH; break; }
        count = p[4];
        if (count > PEEK_MAX_BYTES || !copy_checked(out, (const uint8_t *)get_u32(p), count)) {
            status = BAD_RANGE; count = 0;
        }
        break;
    case CMD_POKE:
        if (bytes < 5) { status = BAD_LENGTH; break; }
        if (!copy_checked((uint8_t *)get_u32(p), p + 4, bytes - 4)) status = BAD_RANGE;
        break;
    case CMD_PANEL:
        if (bytes != PANEL_REQUEST_BYTES) { status = BAD_LENGTH; break; }
        panel_virtual(get_u32(p), p[4] | p[5] << 8, (int8_t)p[6], now_ms);
        panel_pending = 1;
        panel_sequence = sequence;
        return;     /* replied once the presses are done */
    case CMD_SCREENSHOT:
        if (!bytes) {
            screenshot_bytes = encode_screenshot(screen_pixels());
            put_u32(out, screenshot_bytes);
            count = 4;
        } else if (bytes == SCREENSHOT_CHUNK_REQUEST_BYTES) {
            uint32_t offset = p[0] | p[1] << 8;
            count = p[2];
            if (count > SCREENSHOT_MAX_CHUNK || offset + count > screenshot_bytes) { status = BAD_RANGE; count = 0; }
            else memcpy(out, screenshot + offset, count);
        } else status = BAD_LENGTH;
        break;
    case CMD_FILE_BEGIN: {
        char path[REQUEST_BYTES + 1];
        if (!bytes) { status = BAD_LENGTH; break; }
        memcpy(path, p, bytes);
        path[bytes] = 0;
        if (!storage_write_begin(path)) status = BAD_RANGE;
        break;
    }
    case CMD_FILE_DATA:
        if (bytes <= FILE_OFFSET_BYTES) { status = BAD_LENGTH; break; }
        if (!storage_write_at(get_u32(p), p + FILE_OFFSET_BYTES, bytes - FILE_OFFSET_BYTES)) status = BAD_RANGE;
        break;
    case CMD_FILE_END: {
        uint32_t crc, size;
        if (bytes) { status = BAD_LENGTH; break; }
        if (!storage_write_end(&crc, &size)) { status = BAD_RANGE; break; }
        put_u32(out, crc);
        put_u32(out + 4, size);
        count = 8;
        break;
    }
    case CMD_PROMOTE:
        if (bytes != 4) { status = BAD_LENGTH; break; }
        if (get_u32(p) != CONFIRM_WORD || boot_mode != BOOT_TRIAL || !install_promote()) { status = BAD_RANGE; break; }
        boot_mode = BOOT_STABLE;    /* this build is now the installed one */
        break;
    case CMD_RESTORE:
        if (bytes != 4) { status = BAD_LENGTH; break; }
        if (get_u32(p) != CONFIRM_WORD || !install_restore()) status = BAD_RANGE;
        break;
    case CMD_RELOAD_BEGIN:
        if (bytes != 8) { status = BAD_LENGTH; break; }
        if (!reload_begin(get_u32(p), get_u32(p + 4))) status = BAD_RANGE;
        break;
    case CMD_RELOAD_DATA:
        if (bytes <= FILE_OFFSET_BYTES) { status = BAD_LENGTH; break; }
        reload_data(get_u32(p), p + FILE_OFFSET_BYTES, bytes - FILE_OFFSET_BYTES);
        break;
    case CMD_RELOAD_END: {
        uint32_t crc, length;
        if (bytes) { status = BAD_LENGTH; break; }
        if (!reload_end(&crc, &length)) status = BAD_RANGE;
        put_u32(out, crc);
        put_u32(out + 4, length);
        count = 8;
        break;
    }
    case CMD_RELOAD_RUN: {
        int card;
        if (bytes != 4) { status = BAD_LENGTH; break; }
        if (get_u32(p) != CONFIRM_WORD || !reload_keep(&card)) { status = BAD_RANGE; break; }
        out[0] = (uint8_t)card;
        count = 1;
        reload_pending = 1;
        break;
    }
    case CMD_REBOOT:
        if (bytes != 4) { status = BAD_LENGTH; break; }
        if (get_u32(p) != CONFIRM_WORD) { status = BAD_RANGE; break; }
        reboot_pending = 1;
        break;
    default: {
        int handled = editor_command(command, p, bytes, out, sizeof out, &count);
        status = handled < 0 ? BAD_COMMAND : (uint32_t)handled;
        break;
    }
    }
    /* Sequence 0: a change sent as automation, or reload data streamed
     * without waiting, needs no reply */
    if (!sequence && (command == EDITOR_SET || command == CMD_RELOAD_DATA)) return;
    reply(command, sequence, status, out, count);
}

static void dispatch(void)
{
    uint8_t payload[REQUEST_BYTES];
    if (frame_size < SYSEX_HEADER_BYTES || frame[0] != SYSEX_MANUFACTURER ||
        frame[1] != SYSEX_DEVICE_FIRST || frame[2] != SYSEX_DEVICE_SECOND ||
        frame[3] != PROTOCOL_VERSION || frame[4] >= SYSEX_REPLY_FLAG) goto reject;
    uint32_t bytes = frame[6], offset = SYSEX_HEADER_BYTES;
    if (bytes > REQUEST_BYTES || frame_size != SYSEX_HEADER_BYTES + bytes + (bytes + 6) / 7) goto reject;
    for (uint32_t i = 0; i < bytes;) {
        uint32_t high = frame[offset++], used = 0;
        for (; used < SYSEX_GROUP && i < bytes; ++used, ++i)
            payload[i] = frame[offset++] | (((high >> used) & 1u) << 7);
        if (high >> used) goto reject;
    }
    TRACE.last_command = frame[4];
    handle(frame[4], frame[5], payload, bytes);
    return;
reject:
    counters.rx_sysex_rejects++;
}

/* A channel message has one data byte for program change and channel
 * pressure, two otherwise; running status repeats the last status. */
static void channel_byte(uint8_t byte, uint32_t now)
{
    if (!running_status) { counters.rx_stray_data++; return; }
    channel_data[channel_count++] = byte;
    uint32_t type = running_status & MIDI_TYPE_MASK;
    uint32_t needed = type == MIDI_PROGRAM_CHANGE || type == MIDI_CHANNEL_PRESSURE ? 1 : 2;
    if (channel_count < needed) return;
    channel_fn(running_status, channel_data[0], needed > 1 ? channel_data[1] : 0, now);
    channel_count = 0;
}

static void rx_put(uint8_t byte)
{
    uint32_t w = rx_write;
    if (w - rx_read >= RX_BYTES) { counters.rx_sysex_overflows++; return; }
    rx[w & (RX_BYTES - 1)] = byte;
    __DMB();
    rx_write = w + 1;
}

/* Sorts each byte: realtime and channel messages to the engine; SysEx,
 * and a status byte that ends a frame, to rx */
static void uart_isr(void)
{
    while (UARTCharsAvail(BOARD_MIDI_UART)) {
        uint8_t byte = (uint8_t)UARTFIFOCharGet(BOARD_MIDI_UART);
        uint32_t now = cycles_now();
        counters.rx_bytes++;
        if (byte >= MIDI_REALTIME_FIRST) { channel_fn(byte, 0, 0, now); continue; }  /* clock, start, stop */
        if (byte & MIDI_STATUS_BIT) {
            if (byte == SYSEX_START || in_frame) rx_put(byte);
            in_frame = byte == SYSEX_START;
            running_status = byte < MIDI_SYSTEM_FIRST ? byte : 0;
            channel_count = 0;
        } else if (in_frame) rx_put(byte);
        else channel_byte(byte, now);
    }
}

static void receive(uint8_t byte)
{
    if (byte == SYSEX_START) {
        if (frame_active) counters.rx_sysex_rejects++;
        frame_active = 1; frame_size = frame_discard = 0;
        return;
    }
    if (byte == SYSEX_END) {
        if (frame_active && !frame_discard) { counters.rx_sysex_frames++; dispatch(); }
        frame_active = 0;
        return;
    }
    if (byte & MIDI_STATUS_BIT) {
        if (frame_active) counters.rx_sysex_rejects++;
        frame_active = 0;
        return;
    }
    if (!frame_active) return;
    if (frame_size == sizeof frame) {
        if (!frame_discard) counters.rx_sysex_overflows++;
        frame_discard = 1;
    } else frame[frame_size++] = byte;
}

void remote_reset_now(void)
{
    __DSB();
    HWREG(PRM_DEVICE_RSTCTRL) = PRM_RSTCTRL_COLD_SW;
    for (;;) __WFI();
}

void remote_init(uint32_t mode, remote_diagnostics_fn diagnostics, remote_channel_fn channel)
{
    boot_mode = mode;
    diagnostics_fn = diagnostics;
    channel_fn = channel;
    /* UART0 at the MIDI rate, 8N1: the boot loader does not set it up. The receive interrupt comes at every byte. */
    module_clock_enable(SOC_CM_WKUP_REGS + CM_WKUP_UART0_CLKCTRL);
    UARTModuleReset(BOARD_MIDI_UART);
    UARTDivisorLatchWrite(BOARD_MIDI_UART, MIDI_DIVISOR);
    UARTRegConfigModeEnable(BOARD_MIDI_UART, UART_REG_CONFIG_MODE_B);
    UARTLineCharacConfig(BOARD_MIDI_UART, UART_FRAME_WORD_LENGTH_8 | UART_FRAME_NUM_STB_1, UART_PARITY_NONE);
    UARTDivisorLatchDisable(BOARD_MIDI_UART);
    UARTBreakCtl(BOARD_MIDI_UART, UART_BREAK_COND_DISABLE);
    UARTOperatingModeSelect(BOARD_MIDI_UART, UART16x_OPER_MODE);
    unsigned enhanced = UARTEnhanFuncEnable(BOARD_MIDI_UART);
    unsigned tcr_tlr = UARTSubConfigTCRTLRModeEn(BOARD_MIDI_UART);
    HWREG(BOARD_MIDI_UART + UART_TLR) = 0;
    UARTTCRTLRBitValRestore(BOARD_MIDI_UART, tcr_tlr);
    UARTEnhanFuncBitValRestore(BOARD_MIDI_UART, enhanced);
    UARTFIFOConfig(BOARD_MIDI_UART, UART_FIFO_CONFIG(UART_TRIG_LVL_GRANULARITY_1, UART_TRIG_LVL_GRANULARITY_1,
                                                     RX_TRIGGER_BYTES, RX_TRIGGER_BYTES, 0, 0,
                                                     UART_DMA_EN_PATH_SCR, UART_DMA_MODE_0_ENABLE));
    UARTRegConfigModeEnable(BOARD_MIDI_UART, UART_REG_OPERATIONAL_MODE);
    IntRegister(SYS_INT_UART0INT, uart_isr);
    IntPrioritySet(SYS_INT_UART0INT, EVENT_IRQ_PRIORITY, AINTC_HOSTINT_ROUTE_IRQ);
    IntSystemEnable(SYS_INT_UART0INT);
    UARTIntEnable(BOARD_MIDI_UART, UART_INT_RHR_CTI);
}

void remote_poll(uint32_t time_ms)
{
    now_ms = time_ms;
    if (panel_pending && !panel_virtual_busy()) {
        panel_pending = 0;
        reply(CMD_PANEL, panel_sequence, OK, 0, 0);
    }
    counters.foreground_polls++;
    while (rx_read != rx_write) {
        __DMB();
        receive(rx[rx_read & (RX_BYTES - 1)]);
        rx_read++;
    }
    uint32_t room = UART_FIFO_DEPTH - UARTTxFIFOLevelGet(BOARD_MIDI_UART);
    while (room-- && tx_read != tx_write) {
        UARTFIFOCharPut(BOARD_MIDI_UART, tx[tx_read++ & (TX_BYTES - 1)]);
        counters.tx_sysex_bytes++;
    }
    int sent = tx_read == tx_write && (HWREG(BOARD_MIDI_UART + UART_LSR) & UART_LSR_TX_SR_E);
    if (reload_pending && sent) { reload_pending = 0; reload_run(); }
    if (reboot_pending && sent) remote_reset_now();
}

void remote_reboot(void) { reboot_pending = 1; }

uint32_t remote_boot_mode(void) { return boot_mode; }
