/* SSD1351 128x128 RGB565 OLED on the GPMC bus. */
#include "display.h"
#include "board.h"
#include "clock.h"
#include "gpio_v2.h"
#include "gpmc.h"
#include "hw_cm_per.h"

#define COMMAND (*(volatile unsigned char *)BOARD_DISPLAY_BUS)
#define DATA    (*(volatile unsigned char *)(BOARD_DISPLAY_BUS + BOARD_DISPLAY_DATA_OFFSET))

#define GPMC_CS             GPMC_CHIP_SELECT_0
#define GPMC_CS_BASE        (BOARD_DISPLAY_BUS >> 24)

#define SSD1351_COLUMN_ADDRESS      0x15
#define SSD1351_ROW_ADDRESS         0x75
#define SSD1351_WRITE_RAM           0x5c
#define SSD1351_REMAP               0xa0
#define SSD1351_DISPLAY_OFFSET      0xa2
#define SSD1351_NORMAL_DISPLAY      0xa6
#define SSD1351_FUNCTION_SELECT     0xab
#define SSD1351_DISPLAY_OFF         0xae
#define SSD1351_DISPLAY_ON          0xaf
#define SSD1351_PHASE_LENGTH        0xb1
#define SSD1351_CLOCK_DIVIDER       0xb3
#define SSD1351_SEGMENT_LOW_VOLTAGE 0xb4
#define SSD1351_GPIO                0xb5
#define SSD1351_SECOND_PRECHARGE    0xb6
#define SSD1351_GRAY_SCALE_TABLE    0xb8
#define SSD1351_PRECHARGE_VOLTAGE   0xbb
#define SSD1351_VCOMH               0xbe
#define SSD1351_CONTRAST_ABC        0xc1
#define SSD1351_MASTER_CONTRAST     0xc7
#define SSD1351_MUX_RATIO           0xca
#define SSD1351_COMMAND_LOCK        0xfd

#define LOCK_UNLOCK_ALL             0xb1
/* 65k colour, COM split odd/even, COM0 first, C-B-A order, column 0 at
 * SEG127, horizontal increment: the panel's mounting. */
#define REMAP_PANEL                 0x66
#define FUNCTION_INTERNAL_VDD       0x01
#define PHASE_RESET_5_PRECHARGE_3   0x32
#define CLOCK_DIVIDE_2_FASTEST      0xf1
#define VSL_EXTERNAL                0xa0, 0xb5, 0x55
#define GPIO_BOTH_LOW               0x0a
#define SECOND_PRECHARGE_1          0x01
#define PRECHARGE_LEVEL             0x17
#define VCOMH_LEVEL                 0x05
#define CONTRAST_A_B_C              0x88, 0x70, 0x88
#define MASTER_CONTRAST_FULL        0x0f
#define MUX_128                     0x7f
#define LAST_LINE                   (DISPLAY_SIDE - 1)

#define RESET_US                    100
#define POWER_SETTLE_US             200000

static void command(unsigned char c) { COMMAND = c; }
static void data(unsigned char d) { DATA = d; }
static void send(const unsigned char *bytes, unsigned int count)
{
    command(bytes[0]);
    for (unsigned int i = 1; i < count; ++i) data(bytes[i]);
}
#define SEND(...) do { static const unsigned char s_[] = { __VA_ARGS__ }; send(s_, sizeof s_); } while (0)

static void bus_init(void)
{
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_GPMC_CLKCTRL);
    GPMCModuleSoftReset(SOC_GPMC_0_REGS);
    while (!GPMCModuleResetStatusGet(SOC_GPMC_0_REGS)) {}
    GPMCCSConfig(SOC_GPMC_0_REGS, GPMC_CS, GPMC_CS_DISABLE);
    GPMCDevTypeSelect(SOC_GPMC_0_REGS, GPMC_CS, GPMC_DEVICETYPE_NORLIKE);
    GPMCDevSizeSelect(SOC_GPMC_0_REGS, GPMC_CS, GPMC_DEVICESIZE_8BITS);
    GPMCAddrDataMuxProtocolSelect(SOC_GPMC_0_REGS, GPMC_CS, GPMC_MUXADDDATA_NOMUX);
    /* GPMC clock cycles: chip select on 1, off 29; OE 3..23; WE 5..21;
     * read and write cycles 31, read access 23. */
    GPMCCSTimingConfig(SOC_GPMC_0_REGS, GPMC_CS, GPMC_CS_TIMING_CONFIG(29, 29, 0, 1));
    GPMCWEAndOETimingConfig(SOC_GPMC_0_REGS, GPMC_CS,
                            GPMC_WE_OE_TIMING_CONFIG(21, 0, 5, 0, 23, 0, 0, 3));
    GPMCRdAccessAndCycleTimeTimingConfig(SOC_GPMC_0_REGS, GPMC_CS,
                                         GPMC_RDACCESS_CYCLETIME_TIMING_CONFIG(31, 31, 23, 0));
    GPMCBaseAddrSet(SOC_GPMC_0_REGS, GPMC_CS, GPMC_CS_BASE);
    GPMCMaskAddrSet(SOC_GPMC_0_REGS, GPMC_CS, GPMC_CS_SIZE_16MB);
    GPMCCSConfig(SOC_GPMC_0_REGS, GPMC_CS, GPMC_CS_ENABLE);
}

void display_init(void)
{
    bus_init();
    module_clock_enable(SOC_CM_PER_REGS + CM_PER_GPIO2_CLKCTRL);
    GPIOModuleEnable(BOARD_DISPLAY_GPIO);
    GPIODirModeSet(BOARD_DISPLAY_GPIO, BOARD_DISPLAY_RESET_PIN, GPIO_DIR_OUTPUT);
    GPIODirModeSet(BOARD_DISPLAY_GPIO, BOARD_DISPLAY_POWER_PIN, GPIO_DIR_OUTPUT);
    GPIOPinWrite(BOARD_DISPLAY_GPIO, BOARD_DISPLAY_RESET_PIN, GPIO_PIN_LOW);
    delay_us(RESET_US);
    GPIOPinWrite(BOARD_DISPLAY_GPIO, BOARD_DISPLAY_RESET_PIN, GPIO_PIN_HIGH);
    delay_us(RESET_US);

    SEND(SSD1351_COMMAND_LOCK, LOCK_UNLOCK_ALL);
    SEND(SSD1351_DISPLAY_OFF);
    SEND(SSD1351_COLUMN_ADDRESS, 0, LAST_LINE);
    SEND(SSD1351_ROW_ADDRESS, 0, LAST_LINE);
    SEND(SSD1351_REMAP, REMAP_PANEL);
    SEND(SSD1351_DISPLAY_OFFSET, 0);
    SEND(SSD1351_NORMAL_DISPLAY);
    SEND(SSD1351_FUNCTION_SELECT, FUNCTION_INTERNAL_VDD);
    SEND(SSD1351_PHASE_LENGTH, PHASE_RESET_5_PRECHARGE_3);
    SEND(SSD1351_CLOCK_DIVIDER, CLOCK_DIVIDE_2_FASTEST);
    SEND(SSD1351_SEGMENT_LOW_VOLTAGE, VSL_EXTERNAL);
    SEND(SSD1351_GPIO, GPIO_BOTH_LOW);
    SEND(SSD1351_SECOND_PRECHARGE, SECOND_PRECHARGE_1);
    /* Gray scale GS1..GS63: 2 to 19 in steps of 1, then steps of 2 to 39,
     * 3 to 72, 4 to 120 and 5 to 180. */
    static const unsigned char ramp_end[] = { 19, 39, 72, 120, 180 };
    command(SSD1351_GRAY_SCALE_TABLE);
    for (unsigned int level = 2, segment = 0; segment < sizeof ramp_end; ) {
        data(level);
        if (level >= ramp_end[segment]) segment++;
        level += segment + 1;
    }
    SEND(SSD1351_PRECHARGE_VOLTAGE, PRECHARGE_LEVEL);
    SEND(SSD1351_VCOMH, VCOMH_LEVEL);
    SEND(SSD1351_CONTRAST_ABC, CONTRAST_A_B_C);
    SEND(SSD1351_MASTER_CONTRAST, MASTER_CONTRAST_FULL);
    SEND(SSD1351_MUX_RATIO, MUX_128);

    GPIOPinWrite(BOARD_DISPLAY_GPIO, BOARD_DISPLAY_POWER_PIN, GPIO_PIN_LOW);
    delay_us(POWER_SETTLE_US);
    SEND(SSD1351_DISPLAY_ON);
}

static volatile int pending_contrast = -1;

void display_brightness(unsigned level)
{
    if (level < 1) level = 1;
    if (level > DISPLAY_BRIGHTNESS_MAX) level = DISPLAY_BRIGHTNESS_MAX;
    pending_contrast = (int)level - 1;     /* master contrast 0-15 */
}

void display_begin_frame(void)
{
    if (pending_contrast >= 0) {
        const unsigned char contrast[] = { SSD1351_MASTER_CONTRAST, (unsigned char)pending_contrast };
        send(contrast, sizeof contrast);
        pending_contrast = -1;
    }
    SEND(SSD1351_COLUMN_ADDRESS, 0, LAST_LINE);
    SEND(SSD1351_ROW_ADDRESS, 0, LAST_LINE);
    command(SSD1351_WRITE_RAM);
}

void display_write(const uint16_t *pixels, unsigned int count)
{
    for (unsigned int i = 0; i < count; ++i) {
        data(pixels[i] >> 8);
        data(pixels[i]);
    }
}
