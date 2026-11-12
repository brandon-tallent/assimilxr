/* CV and gate inputs.
 *
 * Two 12-input ADCs convert the 24 CV inputs, one on each McSPI link:
 * McSPI0 reads channels 1-4, McSPI1 channels 5-8. At start each ADC gets
 * six setup words and a 16-step conversion sequence; from then on every
 * 16-bit frame returns the conversion of the step before. Each audio
 * block takes the replies from each link's receive FIFO and queues frames
 * to keep 8 in flight (a link stalls with 16), so a sequence takes two
 * blocks. The sequence
 * reads each channel's A input twice, so step n holds:
 *   0-3: A of the link's channels 1-4    8-11:  A again
 *   4-7: B                               12-15: C
 * The gates are active-low GPIO0 inputs, read once a block. An edge
 * interrupt times each gate's first rise in a block, which also catches
 * pulses shorter than a block. */
#include "inputs.h"
#include "board.h"
#include "clock.h"
#include "cpu.h"
#include "audio.h"
#include "interrupt.h"
#include "gpio_v2.h"
#include "hw_mcspi.h"
#include "hw_gpio_v2.h"
#include "hw_cm_per.h"
#include "hw_types.h"
#include <arm_neon.h>
#include <string.h>

#define LINKS                   2u
#define SEQUENCE_STEPS          16u
#define WORD_BYTES              2u
#define SPI_WAIT_LIMIT          100000u

static const uint32_t links[LINKS] = { BOARD_CV_LINK_0, BOARD_CV_LINK_1 };

/* Mode 3, chip select active low, transmit on D1 and receive on D0, the
 * 48 MHz clock divided by 3 with one-cycle granularity: 16 MHz. */
#define SPI_DIVIDE_BY_3         2u
#define SPI_WORD_16             15u
#define SPI_WORD_8              7u
#define SPI_CS_SETUP_1_5        1u      /* clock cycles between frames */
#define SPI_COMMON  (MCSPI_CH0CONF_PHA | MCSPI_CH0CONF_POL | MCSPI_CH0CONF_EPOL | \
                     MCSPI_CH0CONF_DPE0 | MCSPI_CH0CONF_CLKG | \
                     SPI_DIVIDE_BY_3 << MCSPI_CH0CONF_CLKD_SHIFT)
#define SPI_WORDS   (SPI_COMMON | SPI_WORD_16 << MCSPI_CH0CONF_WL_SHIFT)
#define SPI_BYTES   (SPI_COMMON | SPI_WORD_8 << MCSPI_CH0CONF_WL_SHIFT)
/* Both FIFOs on: 32 bytes each, one sequence of 16-bit frames */
#define SPI_STREAM  (SPI_WORDS | MCSPI_CH0CONF_FFEW | MCSPI_CH0CONF_FFER | \
                     SPI_CS_SETUP_1_5 << MCSPI_CH0CONF_TCS0_SHIFT)
#define SPI_MASTER_MULTI        0u

/* ADC setup, one frame per word. The words read as the register writes of
 * a Maxim MAX1113x-type ADC: reset, configuration, unipolar and bipolar
 * input modes, sample-set scan mode, then a sample set of 16 steps. */
#define ADC_RESET_ALL           0x0040u
#define ADC_CONFIGURATION       0x8000u
#define ADC_UNIPOLAR            0x8800u
#define ADC_BIPOLAR             0x9000u
#define ADC_MODE_SAMPLE_SET     0x4800u
#define ADC_SAMPLE_SET          0xb000u
#define ADC_SAMPLE_SET_LENGTH_SHIFT 3
static const uint16_t adc_setup[] = {
    ADC_RESET_ALL, ADC_CONFIGURATION, ADC_UNIPOLAR, ADC_BIPOLAR, ADC_MODE_SAMPLE_SET,
    ADC_SAMPLE_SET | (SEQUENCE_STEPS - 1) << ADC_SAMPLE_SET_LENGTH_SHIFT,
};
/* The sample set: ADC inputs by step, two to a byte, sent in one frame.
 * Inputs 0-3 are the A jacks, 4-7 B, 8-11 C. */
#define STEPS(first, second)    ((first) << 4 | (second))
static const uint8_t adc_sequence[SEQUENCE_STEPS / 2] = {
    STEPS(0, 1), STEPS(2, 3), STEPS(4, 5), STEPS(6, 7),
    STEPS(0, 1), STEPS(2, 3), STEPS(8, 9), STEPS(10, 11),
};

#define STEP_A                  0u
#define STEP_B                  4u
#define STEP_A_AGAIN            8u
#define STEP_C                  12u
#define LINK_CHANNELS           4u

/* Calibration, by CV index: an offset added to the reading, then a Q15
 * gain (negative: the input stage inverts). These defaults are the values
 * of an uncalibrated module until cal.bin is read; a scaled reading of
 * 6553.6 is one volt. */
#define CAL_OFFSET_DEFAULT      (-16384)
#define CAL_GAIN_DEFAULT        (-17920)
#define CAL_SHIFT               2
#define COUNTS_PER_VOLT         6553.6f
#define SMOOTHING               0.3f

/* Gate inputs: GPIO0 pins, channels 1-8 */
static const uint8_t gate_pins[INPUTS_CHANNELS] = { 14, 15, 16, 12, 13, 22, 23, 26 };

static uint16_t ring[LINKS][SEQUENCE_STEPS];
#define IN_FLIGHT               8u
static unsigned step[LINKS];      /* the sequence step of the next reply's conversion */
static unsigned awaited[LINKS];   /* replies still to come from the frames queued */
static int16_t cal_offset[INPUTS_CV], cal_gain[INPUTS_CV];

float inputs_cv[INPUTS_CV];
uint8_t inputs_gates, inputs_gates_rising, inputs_gates_falling, inputs_gates_timed;
uint32_t inputs_gates_cycles[INPUTS_CHANNELS];

static uint32_t gate_mask;      /* the gate pins' GPIO0 bits */
static volatile uint8_t gate_edges;
static volatile uint32_t gate_edge_cycles[INPUTS_CHANNELS];

/* A gate pin fell (the gate rose): its time, then its interrupt waits for
 * the next inputs_sample, so bounces cost nothing */
static void gate_isr(void)
{
    uint32_t now = cycles_now();
    uint32_t status = HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS(0)) & gate_mask;
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS_CLR(0)) = status;
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS(0)) = status;
    for (unsigned c = 0; c < INPUTS_CHANNELS; ++c)
        if (status >> gate_pins[c] & 1u) {
            gate_edge_cycles[c] = now;
            gate_edges |= 1u << c;
        }
}

static int wait_status(uint32_t base, uint32_t bit)
{
    for (unsigned i = 0; i < SPI_WAIT_LIMIT; ++i)
        if (HWREG(base + MCSPI_CHSTAT(0)) & bit) return 1;
    return 0;
}

/* One polled frame; the reply is not used */
static void exchange(uint32_t base, uint32_t word)
{
    if (!wait_status(base, MCSPI_CH0STAT_TXS)) return;
    HWREG(base + MCSPI_TX(0)) = word;
    if (wait_status(base, MCSPI_CH0STAT_RXS)) (void)HWREG(base + MCSPI_RX(0));
}

static void link_setup(uint32_t base)
{
    HWREG(base + MCSPI_SYSCONFIG) = MCSPI_SYSCONFIG_SOFTRESET;
    for (unsigned i = 0; i < SPI_WAIT_LIMIT; ++i)
        if (HWREG(base + MCSPI_SYSSTATUS) & MCSPI_SYSSTATUS_RESETDONE) break;
    HWREG(base + MCSPI_MODULCTRL) = SPI_MASTER_MULTI;
    HWREG(base + MCSPI_CHCONF(0)) = SPI_WORDS;
    HWREG(base + MCSPI_CHCTRL(0)) = MCSPI_CH0CTRL_EN;
    for (unsigned i = 0; i < sizeof adc_setup / sizeof adc_setup[0]; ++i) exchange(base, adc_setup[i]);

    /* The sequence goes in one frame: chip select held for all eight bytes */
    HWREG(base + MCSPI_CHCTRL(0)) = 0;
    HWREG(base + MCSPI_MODULCTRL) = MCSPI_MODULCTRL_SINGLE;
    HWREG(base + MCSPI_CHCONF(0)) = SPI_BYTES | MCSPI_CH0CONF_FORCE;
    HWREG(base + MCSPI_CHCTRL(0)) = MCSPI_CH0CTRL_EN;
    for (unsigned i = 0; i < sizeof adc_sequence; ++i) exchange(base, adc_sequence[i]);
    HWREG(base + MCSPI_CHCONF(0)) = SPI_BYTES;
    HWREG(base + MCSPI_CHCTRL(0)) = 0;
    HWREG(base + MCSPI_MODULCTRL) = SPI_MASTER_MULTI;
    HWREG(base + MCSPI_CHCONF(0)) = SPI_STREAM;
}

/* Queues frames until IN_FLIGHT replies are awaited */
static void send_frames(unsigned link)
{
    for (; awaited[link] < IN_FLIGHT; awaited[link]++) HWREG(links[link] + MCSPI_TX(0)) = 0;
}

/* Stores the replies that have arrived, by sequence step */
static void receive(unsigned link)
{
    uint32_t base = links[link];
    while (awaited[link] && !(HWREG(base + MCSPI_CHSTAT(0)) & MCSPI_CH0STAT_RXFFE)) {
        ring[link][step[link]] = (uint16_t)HWREG(base + MCSPI_RX(0));
        step[link] = (step[link] + 1) % SEQUENCE_STEPS;
        awaited[link]--;
    }
}

void inputs_init(void)
{
    for (unsigned i = 0; i < INPUTS_CV; ++i) {
        cal_offset[i] = CAL_OFFSET_DEFAULT;
        cal_gain[i] = CAL_GAIN_DEFAULT;
    }
    for (unsigned c = 0; c < INPUTS_CHANNELS; ++c) {
        GPIODirModeSet(SOC_GPIO_0_REGS, gate_pins[c], GPIO_DIR_INPUT);
        gate_mask |= 1u << gate_pins[c];
    }
    HWREG(SOC_GPIO_0_REGS + GPIO_FALLINGDETECT) |= gate_mask;
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS(0)) = gate_mask;
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS_SET(0)) = gate_mask;
    IntRegister(SYS_INT_GPIOINT0A, gate_isr);
    IntPrioritySet(SYS_INT_GPIOINT0A, EVENT_IRQ_PRIORITY, AINTC_HOSTINT_ROUTE_IRQ);
    IntSystemEnable(SYS_INT_GPIOINT0A);
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_SPI0_CLKCTRL);
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_SPI1_CLKCTRL);
    for (unsigned l = 0; l < LINKS; ++l) link_setup(links[l]);
    for (unsigned l = 0; l < LINKS; ++l) {
        HWREG(links[l] + MCSPI_CHCTRL(0)) = MCSPI_CH0CTRL_EN;
        step[l] = SEQUENCE_STEPS - 1;   /* the first reply has no conversion before it */
        send_frames(l);
    }
}

static int calibrated;
int inputs_calibrated(void) { return calibrated; }

void inputs_calibrate(const uint8_t *record)
{
    calibrated = 1;
    memcpy(cal_offset, record + INPUTS_CAL_OFFSETS, sizeof cal_offset);
    memcpy(cal_gain, record + INPUTS_CAL_GAINS, sizeof cal_gain);
}

/* Eight readings of one column: the link's four steps from step on each link */
static inline int16x8_t column(unsigned step)
{
    return vreinterpretq_s16_u16(vcombine_u16(vld1_u16(&ring[0][step]), vld1_u16(&ring[1][step])));
}

static inline float32x4x2_t calibrate(int16x8_t raw, unsigned index)
{
    int16x8_t v = vqaddq_s16(raw, vld1q_s16(&cal_offset[index]));
    v = vqshlq_n_s16(vqrdmulhq_s16(v, vld1q_s16(&cal_gain[index])), CAL_SHIFT);
    const float32x4_t scale = vdupq_n_f32(1.0f / COUNTS_PER_VOLT);
    return (float32x4x2_t){ { vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(v))), scale),
                              vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(v))), scale) } };
}

static inline void smooth(float32x4x2_t reading, unsigned index)
{
    for (unsigned h = 0; h < 2; ++h) {
        float *cv = &inputs_cv[index + 4 * h];
        float32x4_t last = vld1q_f32(cv);
        vst1q_f32(cv, vmlaq_n_f32(last, vsubq_f32(reading.val[h], last), SMOOTHING));
    }
}

void inputs_sample(void)
{
    for (unsigned l = 0; l < LINKS; ++l) {
        receive(l);
        send_frames(l);
    }
    _Static_assert(LINKS * LINK_CHANNELS == INPUTS_CHANNELS, "a column is one vector");
    int16x8_t a = vrhaddq_s16(column(STEP_A), column(STEP_A_AGAIN));
    smooth(calibrate(a, inputs_cv_index(0, 0)), inputs_cv_index(0, 0));
    smooth(calibrate(column(STEP_B), inputs_cv_index(0, 1)), inputs_cv_index(0, 1));
    smooth(calibrate(column(STEP_C), inputs_cv_index(0, 2)), inputs_cv_index(0, 2));

    uint32_t irq = irq_save();
    uint32_t pins = HWREG(SOC_GPIO_0_REGS + GPIO_DATAIN);
    uint8_t edges = gate_edges;
    gate_edges = 0;
    for (unsigned c = 0; c < INPUTS_CHANNELS; ++c) inputs_gates_cycles[c] = gate_edge_cycles[c];
    /* Edges since the interrupt was held count in the levels below */
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS(0)) = gate_mask;
    HWREG(SOC_GPIO_0_REGS + GPIO_IRQSTATUS_SET(0)) = gate_mask;
    irq_restore(irq);
    uint8_t gates = 0;
    for (unsigned c = 0; c < INPUTS_CHANNELS; ++c)
        if (!(pins >> gate_pins[c] & 1u)) gates |= 1u << c;
    inputs_gates_rising = (gates & ~inputs_gates) | edges;
    inputs_gates_falling = (inputs_gates | edges) & ~gates;
    inputs_gates_timed = edges;
    inputs_gates = gates;
}
