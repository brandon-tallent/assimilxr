/* Audio streaming: three EDMA ping-pong chains of AUDIO_BLOCK frames.
 *   McASP0 transmit: the eight channel outputs, 48 kHz.
 *   McASP1 transmit: the mix outputs, 192 kHz.
 *   McASP1 receive: the Sample inputs, 192 kHz.
 * The codec is the clock master. Each McASP1 receive block completion
 * renders one block into the transmit halves that have just been sent. */
#include "audio.h"
#include "board.h"
#include "clock.h"
#include "codec.h"
#include "cpu.h"
#include "memory.h"
#include "trace.h"
#include "edma.h"
#include "interrupt.h"
#include "mcasp.h"
#include "hw_cm_per.h"
#include "hw_types.h"
#include <string.h>
#include <arm_neon.h>

#define EDMA                    SOC_EDMA30CC_0_REGS
#define EDMA_QUEUE              0
#define EDMA_CHANNELS           64
#define PARAM_BYTES             32
#define CH_OUTPUT_TX            8   /* McASP0 transmit event */
#define CH_CODEC_TX             10  /* McASP1 transmit event */
#define CH_CODEC_RX             11  /* McASP1 receive event */
#define TCC_RX_PING             11
#define TCC_RX_PONG             9   /* McASP0 receive, unused on this board */
/* Reload PaRAM sets, above the 64 channel sets. */
static const unsigned output_sets[2] = { 64, 65 };
static const unsigned codec_tx_sets[2] = { 66, 67 };
static const unsigned codec_rx_sets[2] = { 68, 69 };
static const unsigned rx_tcc[2] = { TCC_RX_PING, TCC_RX_PONG };

#define SLOTS                   2
#define SLOT_MASK               ((1u << SLOTS) - 1)
#define WORD_BYTES              4
#define OUTPUT_WORDS            (AUDIO_BLOCK * SLOTS * BOARD_OUTPUT_SERIALIZERS)
#define CODEC_FRAMES            (AUDIO_BLOCK * AUDIO_CODEC_OVERSAMPLE)
#define CODEC_WORDS             (CODEC_FRAMES * SLOTS)
#define MCASP_ALL_DATA_BITS     0xffffffffu
#define MCASP_I2S_FORMAT        (MCASP_TX_PAD_WITH_0 | MCASP_TX_BITSTREAM_MSB_FIRST | \
                                 MCASP_TX_SYNC_DELAY_1BIT | \
                                 (MCASP_XFMT_XSSZ_32BITS << MCASP_XFMT_XSSZ_SHIFT) | \
                                 MCASP_TX_BUF_DMAPORT)
#define OUTPUT_BITCLOCK_DIV_4   3   /* register value: divide by 4 */
#define OUTPUT_HIGHCLOCK_DIV_2  1   /* register value: divide by 2 */
#define CLOCK_CHECK_MIN         0x00
#define CLOCK_CHECK_MAX         0xff

#define Q31_TO_FLOAT            (1.0f / 2147483648.0f)
#define BUDGET_CYCLES           (CPU_HZ / AUDIO_RATE * AUDIO_BLOCK)
#define HISTOGRAM_BINS          8

/* [half][frame][slot][serializer] */
static int32_t output_buffer[2][OUTPUT_WORDS] DMA_BUFFER;
static int32_t codec_tx_buffer[2][CODEC_WORDS] DMA_BUFFER;
static int32_t codec_rx_buffer[2][CODEC_WORDS] DMA_BUFFER;

static audio_render_fn render_fn;
static struct audio_input input;
static struct audio_output output;
static float mix_previous[2];
static struct audio_stats stats;
static uint32_t last_entry;

#define Q31_FRACTION_BITS       31

static inline int32x4_t to_q31(float32x4_t x)
{
    return vcvtq_n_s32_f32(x, Q31_FRACTION_BITS);   /* saturates at +-1 */
}

/* Rows a, b, c, d become columns. */
static inline void transpose(int32x4_t *a, int32x4_t *b, int32x4_t *c, int32x4_t *d)
{
    int32x4x2_t ab = vtrnq_s32(*a, *b), cd = vtrnq_s32(*c, *d);
    *a = vcombine_s32(vget_low_s32(ab.val[0]), vget_low_s32(cd.val[0]));
    *b = vcombine_s32(vget_low_s32(ab.val[1]), vget_low_s32(cd.val[1]));
    *c = vcombine_s32(vget_high_s32(ab.val[0]), vget_high_s32(cd.val[0]));
    *d = vcombine_s32(vget_high_s32(ab.val[1]), vget_high_s32(cd.val[1]));
}

static void render_block(unsigned half, uint32_t entry)
{
    _Static_assert(AUDIO_CODEC_OVERSAMPLE == 4 && SLOTS == 2 && BOARD_OUTPUT_SERIALIZERS == 4,
                   "the NEON layout assumes four codec frames and four serializers");

    /* Sample inputs: the average of each four codec frames */
    const int32_t *rx = codec_rx_buffer[half];
    for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
        int32x4x2_t frames = vld2q_s32(rx + i * AUDIO_CODEC_OVERSAMPLE * SLOTS);
        float32x4_t left = vcvtq_f32_s32(frames.val[0]), right = vcvtq_f32_s32(frames.val[1]);
        float32x2_t sum = vpadd_f32(vpadd_f32(vget_low_f32(left), vget_high_f32(left)),
                                    vpadd_f32(vget_low_f32(right), vget_high_f32(right)));
        sum = vmul_n_f32(sum, Q31_TO_FLOAT / AUDIO_CODEC_OVERSAMPLE);
        input.sample[0][i] = vget_lane_f32(sum, 0);
        input.sample[1][i] = vget_lane_f32(sum, 1);
    }

    render_fn(&input, &output, entry);

    /* Channel outputs: frame words are slot 0 of serializers 0-3 (outputs
     * 1, 3, 5, 7), then slot 1 (outputs 2, 4, 6, 8). */
    int32_t *out = output_buffer[half];
    for (unsigned i = 0; i < AUDIO_BLOCK; i += 4) {
        int32x4_t c[AUDIO_CHANNELS];
        for (unsigned n = 0; n < AUDIO_CHANNELS; ++n) c[n] = to_q31(vld1q_f32(&output.channel[n][i]));
        transpose(&c[0], &c[2], &c[4], &c[6]);
        transpose(&c[1], &c[3], &c[5], &c[7]);
        for (unsigned k = 0; k < 4; ++k) {
            int32_t *frame = out + (i + k) * SLOTS * BOARD_OUTPUT_SERIALIZERS;
            vst1q_s32(frame, c[2 * k]);
            vst1q_s32(frame + BOARD_OUTPUT_SERIALIZERS, c[2 * k + 1]);
        }
    }

    /* The mix, interpolated linearly up to the codec rate */
    static const float ramp[AUDIO_CODEC_OVERSAMPLE] = { 0.25f, 0.5f, 0.75f, 1.0f };
    const float32x4_t steps = vld1q_f32(ramp);
    int32_t *tx = codec_tx_buffer[half];
    float previous_l = mix_previous[0], previous_r = mix_previous[1];
    for (unsigned i = 0; i < AUDIO_BLOCK; ++i) {
        float current_l = output.mix[0][i], current_r = output.mix[1][i];
        int32x4x2_t frames;
        frames.val[0] = to_q31(vmlaq_n_f32(vdupq_n_f32(previous_l), steps, current_l - previous_l));
        frames.val[1] = to_q31(vmlaq_n_f32(vdupq_n_f32(previous_r), steps, current_r - previous_r));
        vst2q_s32(tx + i * AUDIO_CODEC_OVERSAMPLE * SLOTS, frames);
        previous_l = current_l;
        previous_r = current_r;
    }
    mix_previous[0] = previous_l;
    mix_previous[1] = previous_r;
}

static void record_timing(uint32_t entry, uint32_t cycles)
{
    uint32_t interval = entry - last_entry;
    last_entry = entry;
    if (stats.blocks) {
        stats.interval_last = interval;
        if (interval < stats.interval_min) stats.interval_min = interval;
        if (interval > stats.interval_max) stats.interval_max = interval;
    }
    stats.blocks++;
    stats.render_cycles_last = cycles;
    if (cycles < stats.render_cycles_min) stats.render_cycles_min = cycles;
    if (cycles > stats.render_cycles_max) stats.render_cycles_max = cycles;
    if (cycles >= BUDGET_CYCLES) stats.over_budget++;
    unsigned bin = cycles * HISTOGRAM_BINS / BUDGET_CYCLES;
    stats.histogram[bin < HISTOGRAM_BINS ? bin : HISTOGRAM_BINS - 1]++;
}

static void edma_completion_isr(void)
{
    uint32_t entry = cycles_now();
    uint32_t pending = EDMA3GetIntrStatus(EDMA) & ((1u << TCC_RX_PING) | (1u << TCC_RX_PONG));
    if (!pending) return;
    EDMA3ClrIntr(EDMA, TCC_RX_PING);
    EDMA3ClrIntr(EDMA, TCC_RX_PONG);
    if (pending == ((1u << TCC_RX_PING) | (1u << TCC_RX_PONG))) {
        stats.dma_errors++; /* a block was missed */
        return;
    }
    unsigned half = pending == (1u << TCC_RX_PONG);
    EDMA3CCPaRAMEntry now;
    EDMA3GetPaRAM(EDMA, CH_OUTPUT_TX, &now);
    stats.output_phase = now.srcAddr - (uint32_t)output_buffer;
    render_block(half, entry);
    record_timing(entry, cycles_now() - entry);
    TRACE.audio_blocks = stats.blocks;
}

/* Writes a ping-pong chain: the channel's own set starts on the ping
 * buffer, and the two reload sets link to each other. A completion
 * interrupt is raised with tcc[half] when tcc is given. */
static void chain(unsigned channel, const unsigned reload[2], EDMA3CCPaRAMEntry p,
                  uint32_t buffer, unsigned half_bytes, int memory_is_source,
                  const unsigned *tcc)
{
    uint32_t opt = p.opt;
    for (unsigned half = 0; half < 2; ++half) {
        uint32_t address = buffer + half * half_bytes;
        if (memory_is_source) p.srcAddr = address; else p.destAddr = address;
        p.linkAddr = reload[!half] * PARAM_BYTES;
        p.opt = opt | (tcc ? EDMA3CC_OPT_TCINTEN | EDMA3CC_OPT_TCC_SET(tcc[half]) : 0);
        EDMA3SetPaRAM(EDMA, reload[half], &p);
        if (!half) EDMA3SetPaRAM(EDMA, channel, &p);
    }
}

static void edma_setup(void)
{
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_TPCC_CLKCTRL);
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_TPTC0_CLKCTRL);
    /* A chain-loaded start finds the previous firmware's channels stopped
     * but configured; clear every channel first. */
    for (unsigned ch = 0; ch < EDMA_CHANNELS; ++ch) {
        EDMA3DisableTransfer(EDMA, ch, EDMA3_TRIG_MODE_EVENT);
        EDMA3DisableEvtIntr(EDMA, ch);
        EDMA3ClrIntr(EDMA, ch);
    }
    EDMA3Init(EDMA, EDMA_QUEUE);
    EDMA3RequestChannel(EDMA, EDMA3_CHANNEL_TYPE_DMA, CH_OUTPUT_TX, CH_OUTPUT_TX, EDMA_QUEUE);
    EDMA3RequestChannel(EDMA, EDMA3_CHANNEL_TYPE_DMA, CH_CODEC_TX, CH_CODEC_TX, EDMA_QUEUE);
    EDMA3RequestChannel(EDMA, EDMA3_CHANNEL_TYPE_DMA, CH_CODEC_RX, TCC_RX_PING, EDMA_QUEUE);
    EDMA3EnableEvtIntr(EDMA, TCC_RX_PONG);
    EDMA3DisableEvtIntr(EDMA, CH_OUTPUT_TX);
    EDMA3DisableEvtIntr(EDMA, CH_CODEC_TX);

    /* Outputs: one event per slot moves one word to each serializer. */
    EDMA3CCPaRAMEntry p = {
        .opt = EDMA3CC_OPT_SYNCDIM, .aCnt = WORD_BYTES, .bCnt = BOARD_OUTPUT_SERIALIZERS,
        .cCnt = AUDIO_BLOCK * SLOTS, .destAddr = BOARD_OUTPUT_MCASP_DATA,
        .srcBIdx = WORD_BYTES, .srcCIdx = WORD_BYTES * BOARD_OUTPUT_SERIALIZERS,
        .bCntReload = BOARD_OUTPUT_SERIALIZERS,
    };
    chain(CH_OUTPUT_TX, output_sets, p, (uint32_t)output_buffer, sizeof output_buffer[0], 1, 0);

    /* Codec: one word per event. */
    EDMA3CCPaRAMEntry tx = {
        .aCnt = WORD_BYTES, .bCnt = CODEC_WORDS, .cCnt = 1,
        .destAddr = BOARD_CODEC_MCASP_DATA, .srcBIdx = WORD_BYTES,
    };
    chain(CH_CODEC_TX, codec_tx_sets, tx, (uint32_t)codec_tx_buffer, sizeof codec_tx_buffer[0], 1, 0);
    EDMA3CCPaRAMEntry rx = {
        .aCnt = WORD_BYTES, .bCnt = CODEC_WORDS, .cCnt = 1,
        .srcAddr = BOARD_CODEC_MCASP_DATA, .destBIdx = WORD_BYTES,
    };
    chain(CH_CODEC_RX, codec_rx_sets, rx, (uint32_t)codec_rx_buffer, sizeof codec_rx_buffer[0], 0, rx_tcc);
}

static void mcasp_common(unsigned int base, unsigned int fifo)
{
    McASPRxReset(base);
    McASPTxReset(base);
    HWREG(fifo + MCASP_FIFO_WFIFOCTL) = 0; /* DMA straight to the serializers */
    HWREG(fifo + MCASP_FIFO_RFIFOCTL) = 0;
    McASPTxFmtMaskSet(base, MCASP_ALL_DATA_BITS);
    McASPTxFmtSet(base, MCASP_I2S_FORMAT);
    McASPTxClkPolaritySet(base, MCASP_TX_CLK_POL_FALL_EDGE);
    McASPTxClkCheckConfig(base, MCASP_TX_CLKCHCK_DIV32, CLOCK_CHECK_MIN, CLOCK_CHECK_MAX);
    McASPTxTimeSlotSet(base, SLOT_MASK);
}

static void output_mcasp_setup(void)
{
    unsigned int base = BOARD_OUTPUT_MCASP;
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_MCASP0_CLKCTRL);
    mcasp_common(base, BOARD_OUTPUT_MCASP_FIFO);
    /* Bit clock and frame sync generated here from the AHCLKX input. */
    McASPTxFrameSyncCfg(base, SLOTS, MCASP_TX_FS_WIDTH_WORD, MCASP_TX_FS_INT_BEGIN_ON_FALL_EDGE);
    McASPTxClkCfg(base, MCASP_TX_CLK_MIXED, OUTPUT_BITCLOCK_DIV_4, OUTPUT_HIGHCLOCK_DIV_2);
    unsigned int serializers = 0;
    for (unsigned s = 0; s < BOARD_OUTPUT_SERIALIZERS; ++s) {
        McASPSerializerTxSet(base, s);
        serializers |= MCASP_PIN_AXR(s);
    }
    McASPPinGPIOSet(base, MCASP_PIN_AFSR | MCASP_PIN_AHCLKR | MCASP_PIN_ACLKR | MCASP_PIN_AMUTE |
                          MCASP_PIN_AXR(4) | MCASP_PIN_AXR(5));
    McASPPinMcASPSet(base, MCASP_PIN_AFSX | MCASP_PIN_AHCLKX | MCASP_PIN_ACLKX | serializers);
    McASPPinDirOutputSet(base, MCASP_PIN_AFSX | MCASP_PIN_ACLKX | serializers);
    McASPPinDirInputSet(base, MCASP_PIN_AHCLKX);
}

static void codec_mcasp_setup(void)
{
    unsigned int base = BOARD_CODEC_MCASP;
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_MCASP1_CLKCTRL);
    mcasp_common(base, BOARD_CODEC_MCASP_FIFO);
    McASPRxFmtMaskSet(base, MCASP_ALL_DATA_BITS);
    McASPRxFmtSet(base, MCASP_I2S_FORMAT);
    /* Clocks and frame sync come from the codec; receive shares them. */
    McASPTxFrameSyncCfg(base, SLOTS, MCASP_TX_FS_WIDTH_WORD, MCASP_TX_FS_EXT_BEGIN_ON_FALL_EDGE);
    McASPRxFrameSyncCfg(base, SLOTS, MCASP_RX_FS_WIDTH_WORD, MCASP_RX_FS_EXT_BEGIN_ON_FALL_EDGE);
    McASPTxClkCfg(base, MCASP_TX_CLK_EXTERNAL, 0, 0);
    McASPRxClkCfg(base, MCASP_RX_CLK_EXTERNAL, 0, 0);
    McASPRxClkPolaritySet(base, MCASP_RX_CLK_POL_RIS_EDGE);
    McASPRxClkCheckConfig(base, MCASP_RX_CLKCHCK_DIV32, CLOCK_CHECK_MIN, CLOCK_CHECK_MAX);
    McASPTxRxClkSyncEnable(base);
    McASPRxTimeSlotSet(base, SLOT_MASK);
    McASPSerializerTxSet(base, BOARD_CODEC_TX_SERIALIZER);
    McASPSerializerRxSet(base, BOARD_CODEC_RX_SERIALIZER);
    McASPPinGPIOSet(base, MCASP_PIN_AFSR | MCASP_PIN_AHCLKR | MCASP_PIN_ACLKR | MCASP_PIN_AMUTE |
                          MCASP_PIN_AXR(2) | MCASP_PIN_AXR(3) | MCASP_PIN_AXR(4) | MCASP_PIN_AXR(5));
    McASPPinMcASPSet(base, MCASP_PIN_AFSX | MCASP_PIN_AHCLKX | MCASP_PIN_ACLKX |
                           MCASP_PIN_AXR(BOARD_CODEC_TX_SERIALIZER) |
                           MCASP_PIN_AXR(BOARD_CODEC_RX_SERIALIZER));
    McASPPinDirOutputSet(base, MCASP_PIN_AHCLKX | MCASP_PIN_AXR(BOARD_CODEC_TX_SERIALIZER));
    McASPPinDirInputSet(base, MCASP_PIN_AFSX | MCASP_PIN_ACLKX |
                              MCASP_PIN_AXR(BOARD_CODEC_RX_SERIALIZER));
}

int audio_start(audio_render_fn render)
{
    render_fn = render;
    memset(output_buffer, 0, sizeof output_buffer);
    memset(codec_tx_buffer, 0, sizeof codec_tx_buffer);
    audio_stats(0, 1);

    edma_setup();
    trace_stage(STAGE_EDMA);
    codec_mcasp_setup();
    output_mcasp_setup();
    trace_stage(STAGE_MCASP);
    int error = codec_init();
    trace_stage(STAGE_CODEC);
    if (error) return error;

    /* Priority 1: the MIDI and gate interrupts (priority 0) time their
     * events while a block renders */
    IntRegister(SYS_INT_EDMACOMPINT, edma_completion_isr);
    IntPrioritySet(SYS_INT_EDMACOMPINT, AUDIO_IRQ_PRIORITY, AINTC_HOSTINT_ROUTE_IRQ);
    IntSystemEnable(SYS_INT_EDMACOMPINT);

    McASPRxClkStart(BOARD_CODEC_MCASP, MCASP_RX_CLK_EXTERNAL);
    McASPTxClkStart(BOARD_CODEC_MCASP, MCASP_TX_CLK_EXTERNAL);
    McASPTxClkStart(BOARD_OUTPUT_MCASP, MCASP_TX_CLK_MIXED);
    EDMA3EnableTransfer(EDMA, CH_CODEC_RX, EDMA3_TRIG_MODE_EVENT);
    EDMA3EnableTransfer(EDMA, CH_CODEC_TX, EDMA3_TRIG_MODE_EVENT);
    EDMA3EnableTransfer(EDMA, CH_OUTPUT_TX, EDMA3_TRIG_MODE_EVENT);
    McASPRxSerActivate(BOARD_CODEC_MCASP);
    McASPTxSerActivate(BOARD_CODEC_MCASP);
    McASPTxSerActivate(BOARD_OUTPUT_MCASP);
    while (McASPTxStatusGet(BOARD_CODEC_MCASP) & MCASP_TX_STAT_DATAREADY) {}
    while (McASPTxStatusGet(BOARD_OUTPUT_MCASP) & MCASP_TX_STAT_DATAREADY) {}

    /* Start both ports together so their blocks stay in step. */
    uint32_t irq = irq_save();
    McASPTxEnable(BOARD_OUTPUT_MCASP);
    McASPRxEnable(BOARD_CODEC_MCASP);
    McASPTxEnable(BOARD_CODEC_MCASP);
    last_entry = cycles_now();
    irq_restore(irq);
    trace_stage(STAGE_AUDIO_STARTED);
    return 0;
}

void audio_stats(struct audio_stats *out, int reset)
{
    uint32_t irq = irq_save();
    if (out) *out = stats;
    if (reset) {
        uint32_t blocks = stats.blocks, errors = stats.dma_errors;
        memset(&stats, 0, sizeof stats);
        stats.blocks = blocks;
        stats.dma_errors = errors;
        stats.render_cycles_min = stats.interval_min = UINT32_MAX;
    }
    irq_restore(irq);
}
