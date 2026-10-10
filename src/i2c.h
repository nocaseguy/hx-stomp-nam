#ifndef HX_I2C_H
#define HX_I2C_H
#include <stdint.h>
/* Polled I2C master (LPC43xx I2C0/I2C1). Returns 0 on success. */
void i2c_init(uint32_t base, uint32_t pclk, uint32_t hz);
int  i2c_write(uint32_t base, uint8_t addr7, const uint8_t *d, uint32_t n);
int  i2c_probe(uint32_t base, uint8_t addr7);   /* 0 if device ACKs */
#endif
