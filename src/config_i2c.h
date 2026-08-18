//config_i2c.h
#ifndef CONFIG_I2C_H
#define CONFIG_I2C_H

#include "hardware/i2c.h"

// Define the underlying hardware block (Simply flip this from i2c1 to i2c0 for debugging)
#define TARGET_I2C_PORT       i2c1	//was i2c0

// Physical pin mapping tied cleanly to the active I2C block configuration
#define TARGET_I2C_SDA_PIN    6	//was 0
#define TARGET_I2C_SCL_PIN    7	//was 1

// Standard hardware clock speeds
#define TARGET_I2C_BAUDRATE   (400 * 1000) // Fast-mode 400kHz profile

#endif

