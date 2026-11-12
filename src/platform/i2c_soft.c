/* I2C in software on the I2C0 pads as GPIO3.5 (SDA) and GPIO3.6 (SCL), for
 * when the controller makes no start condition. A line is released to its
 * pull-up as an input and pulled low as an output, as open drain. */
#include "i2c.h"
#include "clock.h"
#include "cpu.h"
#include "gpio_v2.h"
#include "hw_types.h"
#include "hw_control_AM335x.h"
#include "soc_AM335x.h"

#define GPIO                SOC_GPIO_3_REGS
#define SDA                 5u
#define SCL                 6u
#define HALF_BIT_US         5u      /* 100 kHz */
#define STRETCH_US          1000u
#define PAD_GPIO            (0x40u | 0x20u | 0x10u | 7u)   /* slow slew, receive, pull-up, mode 7 */
#define ADDRESS_WRITE(a)    ((a) << 1)
#define BYTE_BITS           8

static void release(unsigned pin) { GPIODirModeSet(GPIO, pin, GPIO_DIR_INPUT); }
static void pull_low(unsigned pin) { GPIOPinWrite(GPIO, pin, GPIO_PIN_LOW); GPIODirModeSet(GPIO, pin, GPIO_DIR_OUTPUT); }
static int high(unsigned pin) { return GPIOPinRead(GPIO, pin) != 0; }

/* Lets SCL rise, waiting for a target that stretches it */
static int clock_high(void)
{
    release(SCL);
    uint32_t start = cycles_now();
    while (!high(SCL))
        if (cycles_now() - start >= STRETCH_US * CPU_CYCLES_PER_US) return 0;
    delay_us(HALF_BIT_US);
    return 1;
}

/* Sends a byte; returns 1 when the target pulled SDA low to acknowledge */
static int send(uint8_t byte)
{
    for (int bit = BYTE_BITS - 1; bit >= 0; --bit) {
        if (byte >> bit & 1u) release(SDA); else pull_low(SDA);
        delay_us(HALF_BIT_US);
        if (!clock_high()) return 0;
        pull_low(SCL);
    }
    release(SDA);
    delay_us(HALF_BIT_US);
    if (!clock_high()) return 0;
    int acknowledged = !high(SDA);
    pull_low(SCL);
    return acknowledged;
}

int i2c_soft_write(unsigned int address, const unsigned char *data, unsigned int bytes)
{
    HWREG(SOC_CONTROL_REGS + CONTROL_CONF_I2C0_SDA) = PAD_GPIO;
    HWREG(SOC_CONTROL_REGS + CONTROL_CONF_I2C0_SCL) = PAD_GPIO;
    release(SDA);
    release(SCL);
    delay_us(HALF_BIT_US);
    if (!high(SDA) || !high(SCL)) return I2C_TIMEOUT;   /* a line held low */
    pull_low(SDA);                                      /* start */
    delay_us(HALF_BIT_US);
    pull_low(SCL);
    int result = send((uint8_t)ADDRESS_WRITE(address)) ? I2C_OK : I2C_NO_ACK;
    for (unsigned i = 0; result == I2C_OK && i < bytes; ++i)
        if (!send(data[i])) result = I2C_NO_ACK;
    pull_low(SDA);                                      /* stop */
    delay_us(HALF_BIT_US);
    clock_high();
    release(SDA);
    delay_us(HALF_BIT_US);
    return result;
}
