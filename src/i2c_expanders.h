//i2c_expanders.h
/*
#ifndef I2C_EXPANDERS_H
#define I2C_EXPANDERS_H

#include "pico/stdlib.h"

#define I2C_PORT i2c1
#define PCA9685_ADDR 0x40
#define MCP23017_ADDR 0x20

void init_i2c_expanders(void);
void pca9685_set_pwm(uint8_t channel, uint16_t duty);
void mcp23017_write_gpio(uint8_t pin, uint8_t state);

#endif
*/

#ifndef I2C_EXPANDERS_H
#define I2C_EXPANDERS_H

#include "pico/stdlib.h"

// Base address definitions (when all hardware jumper pins A0, A1, A2 are connected to GND)
#define PCA9685_BASE_ADDR   0x40
#define MCP23017_BASE_ADDR  0x20

// API prototypes now accepting the target hardware address
void init_all_expanders(void);
void i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
void pca9685_set_pwm(uint8_t addr, uint8_t channel, uint16_t duty);
void mcp23017_write_gpio(uint8_t addr, uint8_t pin, uint8_t state);

#endif
