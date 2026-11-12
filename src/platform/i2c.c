/* Polled I2C master writes. */
#include "i2c.h"
#include "clock.h"
#include "cpu.h"
#include "hsi2c.h"
#include "hw_hsi2c.h"
#include "hw_types.h"

#define I2C_FUNCTIONAL_HZ   48000000u
#define I2C_INTERNAL_HZ     12000000u
#define I2C_TIMEOUT_US      5000u
#define I2C_STATUS_ALL      0x7fffu
/* Configured while disabled and in reset, then enabled once,
 * which completes the reset. Disabling the controller again resets it, so
 * it is not touched until the reset is done. */
int i2c_init(unsigned int base, unsigned int bus_hz)
{
    I2CMasterDisable(base);
    I2CSoftReset(base);
    I2CMasterInitExpClk(base, I2C_FUNCTIONAL_HZ, I2C_INTERNAL_HZ, bus_hz);
    I2CMasterIntDisableEx(base, I2C_STATUS_ALL);
    I2CMasterEnable(base);
    uint32_t start = cycles_now();
    while (!(I2CSystemStatusGet(base) & I2C_SYSS_RDONE))
        if (cycles_now() - start >= I2C_TIMEOUT_US * CPU_CYCLES_PER_US) return I2C_NOT_READY;
    return I2C_OK;
}

/* Wait for any of the status bits; returns them, or 0 on timeout. */
static unsigned int wait_status(unsigned int base, unsigned int bits)
{
    uint32_t start = cycles_now();
    while (cycles_now() - start < I2C_TIMEOUT_US * CPU_CYCLES_PER_US) {
        unsigned int status = I2CMasterIntRawStatus(base) & bits;
        if (status) return status;
    }
    return 0;
}

int i2c_write(unsigned int base, unsigned int address, const unsigned char *data,
              unsigned int bytes)
{
    int result = I2C_OK;
    while (I2CMasterBusBusy(base)) {}
    I2CMasterIntClearEx(base, I2C_STATUS_ALL);
    I2CMasterSlaveAddrSet(base, address);
    I2CSetDataCount(base, bytes);
    I2CMasterControl(base, I2C_CFG_MST_TX);
    I2CMasterStart(base);
    for (unsigned int i = 0; i < bytes; ++i) {
        unsigned int status = wait_status(base, I2C_INT_TRANSMIT_READY | I2C_INT_NO_ACK |
                                                I2C_INT_ARBITRATION_LOST);
        if (status != I2C_INT_TRANSMIT_READY) {
            result = status ? I2C_NO_ACK : I2C_TIMEOUT;
            break;
        }
        I2CMasterDataPut(base, data[i]);
        I2CMasterIntClearEx(base, I2C_INT_TRANSMIT_READY);
    }
    if (result == I2C_OK) {
        unsigned int status = wait_status(base, I2C_INT_ADRR_READY_ACESS | I2C_INT_NO_ACK);
        if (status != I2C_INT_ADRR_READY_ACESS) result = status ? I2C_NO_ACK : I2C_TIMEOUT;
    }
    I2CMasterStop(base);
    wait_status(base, I2C_INT_STOP_CONDITION);
    I2CMasterIntClearEx(base, I2C_STATUS_ALL);
    return result;
}
