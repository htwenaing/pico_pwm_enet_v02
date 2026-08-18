//i2c_expanders.c
/*

*/

//

#include "i2c_expanders.h"
#include "config_i2c.h"
#include "hardware/i2c.h"

void i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(TARGET_I2C_PORT, addr, buf, 2, false);
}

// Inits a cluster of 4 devices per chip type sequentially
void init_all_expanders(void) {
    for (int i = 0; i < 4; i++) {
        uint8_t pca_addr = PCA9685_BASE_ADDR + i;
        i2c_write_reg(pca_addr, 0x00, 0x10); // Sleep mode to set prescaler
        i2c_write_reg(pca_addr, 0xFE, 121);  // 50Hz baseline clock frequency
        i2c_write_reg(pca_addr, 0x00, 0xA1); // Wake & auto-increment mode
        
        uint8_t mcp_addr = MCP23017_BASE_ADDR + i;
        i2c_write_reg(mcp_addr, 0x00, 0x00); // IODIRA: All outputs
        i2c_write_reg(mcp_addr, 0x01, 0x00); // IODIRB: All outputs
        i2c_write_reg(mcp_addr, 0x12, 0x00); // OLATA: Low states
        i2c_write_reg(mcp_addr, 0x13, 0x00); // OLATB: Low states
    }
}

void pca9685_set_pwm(uint8_t addr, uint8_t channel, uint16_t duty) {
    if (channel > 15) return;
    if (duty > 4095) duty = 4095;

    uint8_t base_reg = 0x06 + (channel * 4);
    uint8_t buf[5];
    buf[0] = base_reg;
    buf[1] = 0x00; 
    buf[2] = 0x00; 
    buf[3] = duty & 0xFF;        
    buf[4] = (duty >> 8) & 0x0F; 

    i2c_write_blocking(TARGET_I2C_PORT, addr, buf, 5, false);
}

void mcp23017_write_gpio(uint8_t addr, uint8_t pin, uint8_t state) {
    if (pin > 15) return;
    
    uint8_t reg = (pin < 8) ? 0x12 : 0x13; 
    uint8_t bit_pos = (pin < 8) ? pin : (pin - 8);

    // Dynamic tracking using an offset look-up index matching the chip identity offset
    uint8_t chip_offset = addr - MCP23017_BASE_ADDR;
    static uint8_t current_gpa[4] = {0};
    static uint8_t current_gpb[4] = {0};
    
    uint8_t *active_cache = (pin < 8) ? &current_gpa[chip_offset] : &current_gpb[chip_offset];

    if (state) {
        *active_cache |= (1 << bit_pos);
    } else {
        *active_cache &= ~(1 << bit_pos);
    }

    i2c_write_reg(addr, reg, *active_cache);
}
