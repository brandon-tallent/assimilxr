#ifndef PLATFORM_I2C_H
#define PLATFORM_I2C_H

enum { I2C_OK, I2C_NO_ACK, I2C_TIMEOUT, I2C_NOT_READY };

/* The module clock must already be on. Resets and configures the
 * controller; I2C_NOT_READY if its reset never completes (no functional
 * clock) */
int i2c_init(unsigned int base, unsigned int bus_hz);
/* The same write in software on the I2C0 pads, which it takes from the
 * controller (i2c_soft.c) */
int i2c_soft_write(unsigned int address, const unsigned char *data, unsigned int bytes);
int i2c_write(unsigned int base, unsigned int address, const unsigned char *data,
              unsigned int bytes);

#endif
