/* Reload: stage a new build from a difference against the image this
 * firmware started from, then jump to it.
 *
 * The difference is a stream of operations, each a varint v:
 *   v even: v / 2 literal bytes follow.
 *   v odd:  v / 2 bytes from the start-up image, at the reference position
 *           moved by a zigzag varint that follows; the position then
 *           advances past them.
 * Varints are 7 bits a byte, low first, the top bit set on all but the
 * last. */
#include "reload.h"
#include "handoff.h"
#include "install.h"
#include "remote.h"
#include "storage.h"
#include "cpu.h"
#include "cache.h"
#include "interrupt.h"
#include "hw_types.h"
#include "soc_AM335x.h"
#include "hw_intc.h"
#include "hw_mcasp.h"
#include "hw_mcspi.h"
#include "hw_tsc_adc_ss.h"
#include "hw_edma3cc.h"
#include "hw_edma3tc.h"
#include "hw_uart_irda_cir.h"
#include "hw_gpio_v2.h"
#include "board.h"
#include <string.h>

/* firmware.ld: the start-up image's copy, the staging area and the
 * trampoline's place, in cached DDR above the heap */
extern uint8_t reload_pristine[], reload_stage[], reload_trampoline[], image_start[], image_end[];
/* start.S: turns the MMU off, copies the image to its load address and
 * jumps to it */
extern uint8_t reload_trampoline_start[], reload_trampoline_end[];

#define SOC_EDMA30TC_0_REGS     0x49800000u     /* the one transfer controller audio clocks */
#define INTC_MIR_BANKS          4u
#define TPTC_BUSY               (EDMA3TC_TCSTAT_PROGBUSY | EDMA3TC_TCSTAT_SRCACTV | EDMA3TC_TCSTAT_WSACTV)
#define QUIESCE_POLLS           1000000u
#define VARINT_MAX_SHIFT        28u

enum state { OP, DELTA, LITERAL, FAILED };

static struct {
    uint32_t length, crc, received, out, reference, value, shift, count;
    enum state state;
    int begun, ready;
} stage;

static uint32_t image_bytes(void) { return (uint32_t)(image_end - image_start); }

int reload_begin(uint32_t length, uint32_t crc)
{
    memset(&stage, 0, sizeof stage);
    if (length != image_bytes()) return 0;
    stage.length = length;
    stage.crc = crc;
    stage.state = OP;
    stage.begun = 1;
    return 1;
}

static void fail(void) { stage.state = FAILED; }

/* One varint byte into value; 1 when the varint is complete */
static int varint_byte(uint8_t byte)
{
    if (stage.shift > VARINT_MAX_SHIFT) { fail(); return 0; }
    stage.value |= (uint32_t)(byte & 0x7fu) << stage.shift;
    stage.shift += 7;
    if (byte & 0x80u) return 0;
    stage.shift = 0;
    return 1;
}

static void copy_reference(int32_t delta)
{
    uint32_t from = stage.reference + (uint32_t)delta, bytes = stage.count;
    if (from > stage.length || bytes > stage.length - from || bytes > stage.length - stage.out) { fail(); return; }
    memcpy(reload_stage + stage.out, reload_pristine + from, bytes);
    stage.out += bytes;
    stage.reference = from + bytes;
}

void reload_data(uint32_t offset, const uint8_t *data, uint32_t bytes)
{
    if (!stage.begun || offset != stage.received) { fail(); return; }
    stage.received += bytes;
    for (uint32_t i = 0; i < bytes && stage.state != FAILED; ++i) {
        uint8_t byte = data[i];
        switch (stage.state) {
        case OP:
            if (!varint_byte(byte)) break;
            stage.count = stage.value >> 1;
            stage.state = stage.value & 1u ? DELTA : stage.count ? LITERAL : OP;
            stage.value = 0;
            if (stage.state == LITERAL && stage.count > stage.length - stage.out) fail();
            break;
        case DELTA:
            if (!varint_byte(byte)) break;
            copy_reference((int32_t)(stage.value >> 1) ^ -(int32_t)(stage.value & 1u));
            stage.value = 0;
            if (stage.state != FAILED) stage.state = OP;
            break;
        case LITERAL: {
            uint32_t take = bytes - i < stage.count ? bytes - i : stage.count;
            memcpy(reload_stage + stage.out, data + i, take);
            stage.out += take;
            stage.count -= take;
            i += take - 1;
            if (!stage.count) stage.state = OP;
            break;
        }
        case FAILED:
            break;
        }
    }
}

int reload_end(uint32_t *crc, uint32_t *length)
{
    *length = stage.out;
    *crc = ~storage_crc32(STORAGE_CRC32_INITIAL, reload_stage, stage.out);
    stage.ready = stage.begun && stage.state == OP && stage.out == stage.length && *crc == stage.crc;
    return stage.ready;
}

static void wait_clear(uint32_t address, uint32_t bits)
{
    for (uint32_t left = QUIESCE_POLLS; left; --left)
        if (!(HWREG(address) & bits)) return;
    remote_reset_now();
}

/* Stops interrupts, audio, the CV inputs and DMA before jumping to the new
 * build, leaving the hardware as the Tallent Edition's trial start does */
static void quiesce(void)
{
    HWREG(BOARD_MIDI_UART + UART_IER) = 0;
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS_CLR(0)) = ~0u;
    for (uint32_t bank = 0; bank < INTC_MIR_BANKS; ++bank) HWREG(SOC_AINTC_REGS + INTC_MIR_SET(bank)) = ~0u;
    HWREG(SOC_AINTC_REGS + INTC_CONTROL) = INTC_CONTROL_NEWIRQAGR;
    HWREG(SOC_AINTC_REGS + INTC_SYSCONFIG) = INTC_SYSCONFIG_SOFTRESET;
    __DSB();
    for (uint32_t left = QUIESCE_POLLS; left; --left)
        if (HWREG(SOC_AINTC_REGS + INTC_SYSSTATUS) & INTC_SYSSTATUS_RESETDONE) break;
    HWREG(SOC_MCASP_0_CTRL_REGS + MCASP_GBLCTL) = 0;
    HWREG(SOC_MCASP_1_CTRL_REGS + MCASP_GBLCTL) = 0;
    HWREG(SOC_SPI_0_REGS + MCSPI_CHCTRL(0)) = 0;
    HWREG(SOC_SPI_1_REGS + MCSPI_CHCTRL(0)) = 0;
    HWREG(SOC_ADC_TSC_0_REGS + TSC_ADC_SS_CTRL) &= ~TSC_ADC_SS_CTRL_ENABLE;
    HWREG(SOC_EDMA30CC_0_REGS + EDMA3CC_S_EECR(0)) = ~0u;
    HWREG(SOC_EDMA30CC_0_REGS + EDMA3CC_S_EECRH(0)) = ~0u;
    HWREG(SOC_EDMA30CC_0_REGS + EDMA3CC_S_QEECR(0)) = ~0u;
    __DSB();
    wait_clear(SOC_EDMA30CC_0_REGS + EDMA3CC_CCSTAT, EDMA3CC_CCSTAT_ACTV);
    wait_clear(SOC_EDMA30TC_0_REGS + EDMA3TC_TCSTAT, TPTC_BUSY);
}

int reload_keep(int *card)
{
    *card = 0;
    if (!stage.ready) return 0;
    if (remote_boot_mode() == BOOT_TRIAL) return 1;
    if (!install_image(reload_stage, stage.length)) return 0;
    *card = install_card_image(reload_stage, stage.length);
    return 1;
}

void reload_run(void)
{
    if (!stage.ready) return;
    /* A trial's reload is a trial again; an installed build's is the new app */
    if (remote_boot_mode() == BOOT_TRIAL) {
        const uint8_t *id = reload_stage + ((const uint8_t *)identity.build_id - image_start);
        for (unsigned i = 0; i < sizeof HANDOFF.build_id; ++i) HANDOFF.build_id[i] = id[i];
        HANDOFF.magic = HANDOFF_TRIAL_MAGIC;
    }
    __asm__ volatile("cpsid if" ::: "memory");
    quiesce();
    memcpy(reload_trampoline, reload_trampoline_start, (uint32_t)(reload_trampoline_end - reload_trampoline_start));
    /* Writes the stage and the trampoline back to DDR; the MMU stays on
     * until the trampoline turns it off */
    CacheDisable(CACHE_ALL);
    __DSB();
    __ISB();
    ((void (*)(const void *, uint32_t))(void *)reload_trampoline)(reload_stage, stage.length);
    for (;;) {}
}
