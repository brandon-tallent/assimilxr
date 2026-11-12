/* Start-up of the audio parts. Two board variants exist:
 * - Cirrus: CS4272 stereo codec (mix outputs, Sample inputs) and CS4382A
 *   eight-channel DAC (the channel outputs).
 * - AKM: AK4558 stereo codec and AK4458 eight-channel DAC. */
#include "codec.h"
#include "board.h"
#include "clock.h"
#include "gpio_v2.h"
#include "i2c.h"
#include "hsi2c.h"
#include "cpu.h"
#include "hw_types.h"
#include "hw_cm_wkup.h"
#include "hw_cm_per.h"

#define I2C_BUS_HZ              100000u
#define RESET_PULSE_US          25
#define I2C_CLOCK_WAIT_US       5000u
#define AKM_POWER_SETTLE_US     25000

/* Cirrus control ports: the first byte is the register address, and bit 7
 * of it makes later bytes go to the following registers. */
#define CIRRUS_AUTO_INCREMENT   0x80

#define CS4272_ADDRESS          0x10
#define CS4272_MODE_CONTROL_1   0x01
#define CS4272_MODE_CONTROL_2   0x07
#define CS4272_QUAD_SPEED       (3u << 6)
#define CS4272_RATIO_128        (2u << 4) /* 24.576 MHz MCLK -> 192 kHz */
#define CS4272_MASTER           (1u << 3)
#define CS4272_DAC_I2S          1u
#define CS4272_DAC_SLOW_FILTER  (1u << 6)
#define CS4272_ATAPI_STEREO     0x09u
#define CS4272_VOLUME_0DB       0u
#define CS4272_ADC_I2S          (1u << 4)
#define CS4272_ADC_HPF_OFF      3u /* both channels: the inputs carry DC */
#define CS4272_CONTROL_PORT     (1u << 1)
#define CS4272_POWER_DOWN       1u

#define CS4382A_ADDRESS         0x18
#define CS4382A_MODE_CONTROL_1  0x01
#define CS4382A_CONTROL_PORT    (1u << 7)
#define CS4382A_I2S             (1u << 4)

/* AK4558 (datasheet 015004500-E-03), written from register 00H */
#define AK4558_ADDRESS          0x10
#define AK4558_POWER_MANAGEMENT 0x00
#define AK4558_POWER_ALL        0x1eu   /* PMADR, PMADL, PMDAR, PMDAL */
#define AK4558_RSTN             1u
#define AK4558_PLL_MCKI_24MHZ   (7u << 1) /* PLL mode 7: reference 24 MHz at MCKI */
#define AK4558_PMPLL            1u
#define AK4558_SDS_DEFAULT      0u
#define AK4558_DIF_32BIT_I2S    (7u << 3) /* stereo, no soft mute */
#define AK4558_DFS_QUAD         (3u << 1) /* MCKS 00, manual speed setting */
#define AK4558_FS_192K          (15u << 3)
#define AK4558_BCKO_64FS        (1u << 1)
#define AK4558_FIRDA_48K        (1u << 5)
#define AK4558_SDDA             (1u << 3) /* DAC short delay filter */
#define AK4558_DEM_OFF          1u
#define AK4558_SDAD             (1u << 2) /* ADC short delay filter, both HPFs off: the inputs carry DC */

/* AK4458, written from register 00H. Only Control 1 differs from its reset
 * value; Control 2 to Control 10 and the channel 1 attenuators are written
 * with their reset values. */
#define AK4458_ADDRESS          0x13
#define AK4458_CONTROL1         0x00
#define AK4458_DIF_32BIT_I2S    (7u << 1)
#define AK4458_RSTN             1u
#define AK4458_CONTROL2_RESET   0x22u
#define AK4458_CONTROL3_RESET   0x00u
#define AK4458_ATT_0DB          0xffu
#define AK4458_CONTROL4_RESET   0x00u
#define AK4458_DSD1_RESET       0x00u
#define AK4458_CONTROL5_RESET   0x03u
#define AK4458_SOUND_RESET      0x00u
#define AK4458_DSD2_RESET       0x00u
#define AK4458_CONTROL6_RESET   0x0du
#define AK4458_CONTROL7_RESET   0x0cu
#define AK4458_CONTROL8_RESET   0x00u
#define AK4458_CONTROL9_RESET   0x00u
#define AK4458_CONTROL10_RESET  0x50u

static int variant;

/* Writes go through the controller; if it fails, the rest go in software */
static int soft;
static int write(unsigned int address, const unsigned char *data, unsigned int bytes)
{
    if (!soft) {
        int error = i2c_write(BOARD_I2C, address, data, bytes);
        if (!error) return 0;
        I2CMasterDisable(BOARD_I2C);
        soft = 1;
    }
    return i2c_soft_write(address, data, bytes);
}

static int cirrus_init(void)
{
    static const unsigned char cs4272_hold[] = {
        CS4272_MODE_CONTROL_2, CS4272_CONTROL_PORT | CS4272_POWER_DOWN,
    };
    static const unsigned char cs4272_setup[] = {
        CIRRUS_AUTO_INCREMENT | CS4272_MODE_CONTROL_1,
        CS4272_QUAD_SPEED | CS4272_RATIO_128 | CS4272_MASTER | CS4272_DAC_I2S,
        CS4272_DAC_SLOW_FILTER,
        CS4272_ATAPI_STEREO,
        CS4272_VOLUME_0DB, CS4272_VOLUME_0DB,
        CS4272_ADC_I2S | CS4272_ADC_HPF_OFF,
        CS4272_CONTROL_PORT, /* and power up */
    };
    static const unsigned char cs4382a_setup[] = {
        CIRRUS_AUTO_INCREMENT | CS4382A_MODE_CONTROL_1,
        CS4382A_CONTROL_PORT, /* all pairs powered */
        CS4382A_I2S,
        0,
    };
    int error = write(CS4272_ADDRESS, cs4272_hold, sizeof cs4272_hold);
    if (!error) error = write(CS4272_ADDRESS, cs4272_setup, sizeof cs4272_setup);
    if (!error) error = write(CS4382A_ADDRESS, cs4382a_setup, sizeof cs4382a_setup);
    return error;
}

static int akm_init(void)
{
    /* Master at 192 kHz from the PLL, 32-bit I2S */
    static const unsigned char codec[] = {
        AK4558_POWER_MANAGEMENT,
        AK4558_POWER_ALL | AK4558_RSTN,                     /* 00H Power Management */
        AK4558_PLL_MCKI_24MHZ | AK4558_PMPLL,               /* 01H PLL Control */
        AK4558_SDS_DEFAULT,                                 /* 02H DAC TDM */
        AK4558_DIF_32BIT_I2S,                               /* 03H Control 1 */
        AK4558_DFS_QUAD,                                    /* 04H Control 2 */
        AK4558_FS_192K | AK4558_BCKO_64FS,                  /* 05H Mode Control */
        AK4558_FIRDA_48K | AK4558_SDDA | AK4558_DEM_OFF,    /* 06H Filter Setting */
        AK4558_SDAD,                                        /* 07H HPF Enable, Filter Setting */
    };
    static const unsigned char dac[] = {
        AK4458_CONTROL1,
        AK4458_DIF_32BIT_I2S | AK4458_RSTN,                 /* 00H Control 1 */
        AK4458_CONTROL2_RESET, AK4458_CONTROL3_RESET,
        AK4458_ATT_0DB, AK4458_ATT_0DB,                     /* 03H, 04H channel 1 */
        AK4458_CONTROL4_RESET, AK4458_DSD1_RESET, AK4458_CONTROL5_RESET,
        AK4458_SOUND_RESET, AK4458_DSD2_RESET, AK4458_CONTROL6_RESET,
        AK4458_CONTROL7_RESET, AK4458_CONTROL8_RESET, AK4458_CONTROL9_RESET,
        AK4458_CONTROL10_RESET,                             /* 0EH Control 10 */
    };
    delay_us(AKM_POWER_SETTLE_US);
    int error = write(AK4558_ADDRESS, codec, sizeof codec);
    if (!error) error = write(AK4458_ADDRESS, dac, sizeof dac);
    return error;
}

int codec_init(void)
{
    module_clock_enable(SOC_CM_WKUP_REGS + CM_WKUP_I2C0_CLKCTRL);
    /* Wait for the I2C0 functional clock before the controller is used */
    uint32_t start = cycles_now();
    while (!(HWREG(SOC_CM_WKUP_REGS + CM_WKUP_CLKSTCTRL) & CM_WKUP_CLKSTCTRL_CLKACTIVITY_I2C0_GFCLK))
        if (cycles_now() - start >= I2C_CLOCK_WAIT_US * CPU_CYCLES_PER_US) return I2C_NOT_READY;
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_GPIO2_CLKCTRL);
    GPIOModuleEnable(BOARD_AUDIO_RESET_GPIO);
    GPIODirModeSet(BOARD_AUDIO_VARIANT_GPIO, BOARD_AUDIO_VARIANT_PIN, GPIO_DIR_INPUT);
    variant = GPIOPinRead(BOARD_AUDIO_VARIANT_GPIO, BOARD_AUDIO_VARIANT_PIN)
                  ? CODEC_VARIANT_AKM : CODEC_VARIANT_CIRRUS;

    GPIODirModeSet(BOARD_MIX_ENABLE_GPIO, BOARD_MIX_ENABLE_PIN, GPIO_DIR_OUTPUT);
    codec_mix_enable(0);
    GPIODirModeSet(BOARD_AUDIO_RESET_GPIO, BOARD_AUDIO_RESET_PIN_A, GPIO_DIR_OUTPUT);
    GPIODirModeSet(BOARD_AUDIO_RESET_GPIO, BOARD_AUDIO_RESET_PIN_B, GPIO_DIR_OUTPUT);
    GPIOPinWrite(BOARD_AUDIO_RESET_GPIO, BOARD_AUDIO_RESET_PIN_B, GPIO_PIN_LOW);
    GPIOPinWrite(BOARD_AUDIO_RESET_GPIO, BOARD_AUDIO_RESET_PIN_A, GPIO_PIN_LOW);
    delay_us(RESET_PULSE_US);
    GPIOPinWrite(BOARD_AUDIO_RESET_GPIO, BOARD_AUDIO_RESET_PIN_B, GPIO_PIN_HIGH);
    GPIOPinWrite(BOARD_AUDIO_RESET_GPIO, BOARD_AUDIO_RESET_PIN_A, GPIO_PIN_HIGH);

    int error = i2c_init(BOARD_I2C, I2C_BUS_HZ);
    if (error) return error;
    return variant == CODEC_VARIANT_AKM ? akm_init() : cirrus_init();
}

int codec_variant(void) { return variant; }

void codec_mix_enable(int on)
{
    GPIOPinWrite(BOARD_MIX_ENABLE_GPIO, BOARD_MIX_ENABLE_PIN, on ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}
