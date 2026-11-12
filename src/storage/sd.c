/* SD cards: the StarterWare MMC/SD protocol layer on the two MMCHS
 * controllers, with polled callbacks. The CPU moves the data through the
 * controller's data register, so no EDMA channel or interrupt is used and
 * transfers run in the main loop while audio plays. Both cards run a
 * 1-bit bus at 3.0 V with the clock capped at 24 MHz. */
#include "sd.h"
#include "board.h"
#include "clock.h"
#include "cpu.h"
#include "hs_mmcsd.h"
#include "hw_hs_mmcsd.h"
#include "mmcsd_proto.h"
#include "hs_mmcsdlib.h"
#include "hw_cm_per.h"
#include "hw_types.h"
#include <string.h>

#define CONTROLLER_HZ       96000000u   /* the MMCHS functional clock */
#define IDENTIFY_HZ         400000u
#define CLOCK_MAX_HZ        24000000u
#define COMMAND_TIMEOUT_US  20000u
#define DATA_TIMEOUT_US     500000u     /* a card may take 250 ms to program a block */
#define WORD_BYTES          4u
#define MODULEMODE_MASK     3u
#define MODULEMODE_ENABLE   2u

#define STATUS_EVENTS       (HS_MMCSD_INTR_CMDCOMP | HS_MMCSD_INTR_TRNFCOMP | HS_MMCSD_INTR_BUFRDRDY | \
                             HS_MMCSD_INTR_BUFWRRDY | HS_MMCSD_INTR_CMDTIMEOUT | HS_MMCSD_INTR_DATATIMEOUT | \
                             HS_MMCSD_INTR_CMDCRCERR | HS_MMCSD_INTR_DATACRCERR | HS_MMCSD_INTR_CMDBITERR | \
                             HS_MMCSD_INTR_DATABITERR | HS_MMCSD_INTR_CMDINDXERR | HS_MMCSD_INTR_CARDERROR)
#define STATUS_ALL          0xffffffffu

struct drive {
    mmcsdCtrlInfo ctrl;
    mmcsdCardInfo card;
    uint8_t *data;              /* the transfer set up by xfer_setup */
    uint32_t block_bytes, blocks;
    int reading, ready;
};

static struct drive drives[2];

/* How far each drive's start got, for Peek: the step reached, then the
 * controller's status and present-state registers there */
enum sd_step { SD_STEP_CLOCK = 1, SD_STEP_CONTROLLER, SD_STEP_CARD, SD_STEP_READY };
struct sd_trace { uint32_t step, status, present, card_type, failed_command, failed_status, failed_in_data; };
volatile struct sd_trace sd_trace[2];

static const uint32_t controllers[2] = { BOARD_SD_SYSTEM, BOARD_SD_CARD };

static int wait_status(uint32_t base, uint32_t events, uint32_t timeout_us)
{
    uint32_t start = cycles_now(), limit = timeout_us * CPU_CYCLES_PER_US;
    do {
        uint32_t status = HWREG(base + MMCHS_STAT);
        if (status & MMCHS_STAT_ERRI) return 0;
        if (status & events) return 1;
    } while (cycles_now() - start < limit);
    return 0;
}

#define CMD_INDEX_SHIFT     24
static void clear_error(uint32_t base, unsigned int lines)
{
    volatile struct sd_trace *t = &sd_trace[base == controllers[0] ? 0 : 1];
    t->failed_command = HWREG(base + MMCHS_CMD) >> CMD_INDEX_SHIFT;
    t->failed_status = HWREG(base + MMCHS_STAT);
    t->failed_in_data = lines != HS_MMCSD_CMDLINE_RESET;
    HWREG(base + MMCHS_STAT) = STATUS_ALL;
    HSMMCSDLinesReset(base, lines);
}

static unsigned int command_status(mmcsdCtrlInfo *ctrl)
{
    if (!wait_status(ctrl->memBase, MMCHS_STAT_CC, COMMAND_TIMEOUT_US)) {
        clear_error(ctrl->memBase, HS_MMCSD_CMDLINE_RESET);
        return 0;
    }
    HWREG(ctrl->memBase + MMCHS_STAT) = MMCHS_STAT_CC;
    return 1;
}

static struct drive *drive_of(mmcsdCtrlInfo *ctrl)
{
    return ctrl->memBase == controllers[0] ? &drives[0] : &drives[1];
}

static void transfer_setup(mmcsdCtrlInfo *ctrl, unsigned char reading, void *data,
                           unsigned int block_bytes, unsigned int blocks)
{
    struct drive *d = drive_of(ctrl);
    d->data = data;
    d->block_bytes = block_bytes;
    d->blocks = blocks;
    d->reading = reading;
    HSMMCSDBlkLenSet(ctrl->memBase, block_bytes);
}

/* Moves the blocks word by word as the controller's buffer allows. The
 * library asks again after the stop command that ends a multi-block
 * transfer; with the blocks moved, that only waits for the card. */
static unsigned int transfer_status(mmcsdCtrlInfo *ctrl)
{
    struct drive *d = drive_of(ctrl);
    uint32_t base = ctrl->memBase, ready = d->reading ? MMCHS_STAT_BRR : MMCHS_STAT_BWR;
    uint8_t *data = d->data;
    for (uint32_t b = 0; b < d->blocks; ++b) {
        if (!wait_status(base, ready, DATA_TIMEOUT_US)) goto failed;
        HWREG(base + MMCHS_STAT) = ready;
        for (uint32_t i = 0; i < d->block_bytes; i += WORD_BYTES, data += WORD_BYTES) {
            uint32_t word;
            if (d->reading) { word = HWREG(base + MMCHS_DATA); memcpy(data, &word, WORD_BYTES); }
            else { memcpy(&word, data, WORD_BYTES); HWREG(base + MMCHS_DATA) = word; }
        }
    }
    d->blocks = 0;
    if (!wait_status(base, MMCHS_STAT_TC, DATA_TIMEOUT_US)) goto failed;
    HWREG(base + MMCHS_STAT) = MMCHS_STAT_TC;
    return 1;
failed:
    clear_error(base, HS_MMCSD_DATALINE_RESET | HS_MMCSD_CMDLINE_RESET);
    return 0;
}

/* Status flags only: nothing reaches the interrupt controller */
static void status_enable(mmcsdCtrlInfo *ctrl)
{
    HSMMCSDIntrStatusEnable(ctrl->memBase, ctrl->intrMask);
}

static int bus_clock(mmcsdCtrlInfo *ctrl, unsigned int hz)
{
    return HSMMCSDBusFreqSet(ctrl->memBase, ctrl->ipClk, hz > CLOCK_MAX_HZ ? CLOCK_MAX_HZ : hz, 0);
}

/* MMCHS2 is in the L3S domain: wait for the module mode only */
static void clock_enable(unsigned drive)
{
    if (drive == 0) { module_clock_enable(SOC_CM_PER_REGS + CM_PER_MMC0_CLKCTRL); return; }
    uint32_t clkctrl = SOC_CM_PER_REGS + CM_PER_MMC2_CLKCTRL;
    HWREG(clkctrl) = (HWREG(clkctrl) & ~MODULEMODE_MASK) | MODULEMODE_ENABLE;
    while ((HWREG(clkctrl) & MODULEMODE_MASK) != MODULEMODE_ENABLE) {}
}

int sd_init(unsigned drive)
{
    struct drive *d = &drives[drive];
    memset(d, 0, sizeof *d);
    sd_trace[drive].step = SD_STEP_CLOCK;
    clock_enable(drive);
    mmcsdCtrlInfo *c = &d->ctrl;
    c->memBase = controllers[drive];
    c->ipClk = CONTROLLER_HZ;
    c->opClk = IDENTIFY_HZ;
    c->ctrlInit = HSMMCSDControllerInit;
    c->cmdSend = HSMMCSDCmdSend;
    c->busWidthConfig = HSMMCSDBusWidthConfig;
    c->busFreqConfig = bus_clock;
    c->cmdStatusGet = command_status;
    c->xferStatusGet = transfer_status;
    c->xferSetup = transfer_setup;
    c->cardPresent = HSMMCSDCardPresent;
    c->intrEnable = status_enable;
    c->intrMask = STATUS_EVENTS;
    c->busWidth = SD_BUS_WIDTH_1BIT;
    c->card = &d->card;
    d->card.ctrl = c;
    volatile struct sd_trace *t = &sd_trace[drive];
    t->step = SD_STEP_CONTROLLER;
    int ok = MMCSDCtrlInit(c);
    if (ok) {
        status_enable(c);   /* the library leaves this to its caller */
        t->step = SD_STEP_CARD;
        ok = MMCSDCardInit(c);
    }
    t->status = HWREG(c->memBase + MMCHS_STAT);
    t->present = HWREG(c->memBase + MMCHS_PSTATE);
    t->card_type = d->card.cardType | d->card.highCap << 8 | d->card.sd_ver << 16;
    if (!ok) return 0;
    MMCSDTranSpeedSet(c);
    t->step = SD_STEP_READY;
    d->ready = 1;
    return 1;
}

int sd_ready(unsigned drive) { return drives[drive].ready; }

uint32_t sd_blocks(unsigned drive) { return drives[drive].card.nBlks; }

int sd_read(unsigned drive, void *buffer, uint32_t block, uint32_t count)
{
    return drives[drive].ready && MMCSDReadCmdSend(&drives[drive].ctrl, buffer, block, count);
}

int sd_write(unsigned drive, const void *buffer, uint32_t block, uint32_t count)
{
    return drives[drive].ready && MMCSDWriteCmdSend(&drives[drive].ctrl, (void *)buffer, block, count);
}
