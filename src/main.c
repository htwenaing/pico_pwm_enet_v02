/*
3 phase voltage, current generator for simulation (by: Htwe Naing, last updated: 7/26/26)

Raspberry Pi Pico, 
which uses a special UF2 bootloader to let you load programs by dragging and dropping files onto a virtual USB drive.
How the Drag-and-Drop Process Works
Boot Mode: Press and hold the white BOOTSEL button on the board while plugging the USB cable into your computer.
Drive Mount: The Pico shows up on your computer screen as a small removable USB drive named RPI-RP2 (or RP2350 for Pico 2).
File Transfer: Drag and drop a special .uf2 binary format file directly onto that drive.
Auto-Reboot: The board reads the file, flashes it to its internal memory, 
and restarts itself to run your code.

bash
# 1. Create and navigate to the build directory
mkdir build && cd build

# 2. Configure the project using the Ninja generator
cmake -G Ninja ..

# 3. Compile the project
ninja

******** using picotool for programming *********** 
.\picotool.exe load -f .\pico_pwm_example.uf2


*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
// Bring in your isolated interface modules
#include "usb_console.h"   // Handles USB Serial/UART command console
#include "network_stack.h" // Handles W5100S DHCP, DNS, and TCP Server Port 5000
// Bring in your underlying project system components
#include "wave_engine.h"
#include "i2c_expanders.h"
#include "hardware/i2c.h"
#include "config_i2c.h" 
#include "network_stack.h"
#include "i2c_expanders.h"
#include "ssd1306.h"
//#include "usb_console.h" // Include your new isolated header module


#define LED_HEARTBEAT_PIN   25  // 25 is the native onboard LED pin for standard

volatile bool use_hardware_sync = false; // Starts generating waves immediately at power-on


void init_system_i2c_bus(void) {
    i2c_init(TARGET_I2C_PORT, TARGET_I2C_BAUDRATE); 
    
    gpio_set_function(TARGET_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(TARGET_I2C_SCL_PIN, GPIO_FUNC_I2C);
    
    gpio_pull_up(TARGET_I2C_SDA_PIN);
    gpio_pull_up(TARGET_I2C_SCL_PIN);
}

void update_display_status(void) {
    char ip_addr[16];
    ssd1306_clear();
    
    // Use large text mode for high visibility on 128x32 screens
    text_scale = 1; 
    ssd1306_draw_string(0, 0, "W5100S SINE");
    /*
    if (get_ip_assigned_status(ip_addr)) {
        text_scale = 1; // Drop to standard size to fit the full IP string
        ssd1306_draw_string(0, 16, "DHCP: CONNECTED");
        ssd1306_draw_string(0, 24, ip_addr);
    } else {
        text_scale = 1;
        ssd1306_draw_string(0, 16, "DHCP: SEARCHING...");
    }
	*/
    ssd1306_show();	
}

int main(void) {
	
	// Add this variable right before your while(true) block starts
    uint32_t last_heartbeat_time = 0;	
	
	// Initialize the onboard LED Pin
    gpio_init(LED_HEARTBEAT_PIN);
    gpio_set_dir(LED_HEARTBEAT_PIN, GPIO_OUT);
    
    // Tracking variable for our non-blocking timer window
    uint32_t last_led_toggle_time = 0;
    bool led_state = false;

    printf("\r\n[BOOT] Complete! Non-blocking main loop running...\r\nPICO> ");
    fflush(stdout);	
	
    // ====================================================================
    // 1. HARDWARE SYSTEM CORE INITIALIZATIONS (Core 0 Setup)
    // ====================================================================
    
    // Initialize standard outputs (Clears the path for USB/UART printfs)
    init_usb_console();
    
    printf("\r\n=========================================================\r\n");
    printf("   6-CHANNEL INTERLEAVED WAVE GENERATOR OPERATIONAL BOOT  \r\n");
    printf("=========================================================\r\n");
	
    stdio_init_all();
    init_system_i2c_bus();
    ssd1306_init();
    init_all_expanders();   
    init_wave_engine();	
	
    // 4. Fire up hardware-timed interrupts on Core 1 AFTER network structures are allocated
    multicore_launch_core1(core1_entry);	
	
	//test
	update_display_status();

    // Initialize your shared cross-core thread protector boundaries
    //critical_section_init(&wave_crit_sec);

    // Initialize the physical Wiznet W5100S chip (DHCP Engine, 1ms Hardware Timer)
    printf("[BOOT] Initializing W5100S Hardwired TCP/IP Ethernet block...\n");
    init_network_stack();

    // ====================================================================
    // 2. LAUNCH DETERMINISTIC SYNTHESIZER ENGINE (Core 1 Release)
    // ====================================================================
    printf("[BOOT] Mounting Waveform Synthesizer Engine to Core 1...\n");
    // multicore_launch_core1(core1_wave_synth_entry);

    printf("\r\n[BOOT] Complete! Waiting for GP28 Sync Edge or AT+SYNCBYPASS...\r\n");
    printf("PICO> ");
    fflush(stdout);

    // ====================================================================
    // 3. LOW-PRIORITY IO & INTERFACE PROCESSING LOOP (Core 0 Processing)
    // ====================================================================
    while (true) {
		
        // ====================================================================
        // VISUAL HEARTBEAT ENGINE: Toggles the LED every 500ms (1Hz blink rate)
        // ====================================================================
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_led_toggle_time >= 500) {
            last_led_toggle_time = current_time;
            
            led_state = !led_state;
            gpio_put(LED_HEARTBEAT_PIN, led_state);
        }		
		
		
        // A. Continuous, non-blocking polling execution for AT Console inputs
        process_usb_console_loop();
        
        // B. Continuous, non-blocking evaluation of Wiznet lease shifts and Port 5000 
        process_network_loop();
        
        // C. Asynchronous OLED refresh window logic
        // (Ensure this code has NO delays or long blocking loops!)
        // if (should_refresh_oled_buffers()) {
        //     refresh_oled_display_layout();
        // }
    }

    return 0;
}

