/*
3 phase voltage, current generator for simulation (by: Htwe Naing, last updated: 8/19/26) working ok, but not fully tested yet

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

//must set under \ioLibrary_Driver\Ethernetwizchip_conf.h, if not set on CMakeLists.txt
#ifndef _WIZCHIP_
#define _WIZCHIP_                      W5100S   // W5100, W5100S, W5200, W5300, W5500
#endif


***** out *********
pico_sine_team_project/
├── CMakeLists.txt             <-- Team build script
├── pico_sdk_import.cmake      
├── lib/
│   └── libcore_engine.a       <-- Put your compiled .a file here
└── src/
    ├── at_commands.c          <-- THE ONLY EDITABLE CODE FILE
    ├── at_commands.h
    ├── config_i2c.h
    ├── i2c_expanders.h	        
    ├── network_stack.h
	├── ssd1306.h
    ├── usb_console.h
    └── wave_engine.h

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
#include "at_commands.h"


#define LED_HEARTBEAT_PIN   25  // 25 is the native onboard LED pin for standard

// Define master selection control here
// Set to true = Run ONLY via Ethernet network on port 5000
// Set to false = Run ONLY via local USB/UART hardware terminal
//volatile bool use_network_interface = false;
volatile bool use_network_interface = true;

volatile bool use_hardware_sync = false; // Starts generating waves immediately at power-on

// Bring in the global network indicators from network_stack.c
extern char current_ip_str[32];
extern uint8_t g_dhcp_get_ip_flag;


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
	
	// Add these tracking variables right before your while(true) block starts
    uint32_t last_oled_refresh_time = 0;
    uint8_t last_known_dhcp_state = 0xFF; // Forces an initial print state	

    printf("\r\n[BOOT] Complete! Non-blocking main loop running...\r\nPICO> ");
    fflush(stdout);	
	
    // ====================================================================
    // 1. HARDWARE SYSTEM CORE INITIALIZATIONS (Core 0 Setup)
    // ====================================================================
    
    // 1. Always execute basic console initializations to open standard output handles
    init_usb_console();
	   
    printf("\r\n=========================================================\r\n");
    printf("   6-CHANNEL INTERLEAVED WAVE GENERATOR OPERATIONAL BOOT  \r\n");
    printf("=========================================================\r\n");
	
    stdio_init_all();
    init_system_i2c_bus();
    ssd1306_init();
    init_all_expanders();   
    init_wave_engine();	
	
    // Initialize the physical Wiznet W5100S chip (DHCP Engine, 1ms Hardware Timer)
    //printf("[BOOT] Initializing W5100S Hardwired TCP/IP Ethernet block...\n");
	
    // 2. Initialize the selective interface stack based on choice selection
    if (use_network_interface) {
        printf("[SYSTEM] Booting up in Network Stack mode exclusively...\n");
        init_network_stack();
    } else {
		//init_usb_console();
        printf("[SYSTEM] Booting up in Local USB Serial interface mode exclusively...\n");
    }	
			
    // 4. Fire up hardware-timed interrupts on Core 1 AFTER network structures are allocated
    multicore_launch_core1(core1_entry);	//this line must be after wave and net init
	
	//test
	update_display_status();

    // Initialize your shared cross-core thread protector boundaries
    critical_section_init(&wave_crit_sec);

    // ====================================================================
    // 2. LAUNCH DETERMINISTIC SYNTHESIZER ENGINE (Core 1 Release)
    // ====================================================================
    printf("[BOOT] Mounting Waveform Synthesizer Engine to Core 1...\n");
    //multicore_launch_core1(core1_wave_synth_entry);	//doesn't exist anymore

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


        // ====================================================================
        // ASYNCHRONOUS OLED STATUS ENGINE (Refreshes once every 1000ms)
        // ====================================================================
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_oled_refresh_time >= 1000) {
            last_oled_refresh_time = now;

            // Clear your OLED buffer (use your project's native clear function name)
            ssd1306_clear(); 

            // Line 1: Always draw the generator status context
            ssd1306_draw_string(0, 0, "6-CH WAVE GENERATOR");

            // Line 2: Alternate text based on whether the network or USB console is active
            if (use_network_interface) {
                if (g_dhcp_get_ip_flag == 1) {
                    // Create a display string buffer and render the current active IP address
                    char oled_ip_buffer[32];
                    snprintf(oled_ip_buffer, sizeof(oled_ip_buffer), "IP: %s", current_ip_str);
                    ssd1306_draw_string(0, 16, oled_ip_buffer);
                } else {
                    ssd1306_draw_string(0, 16, "IP: SEARCHING...");
                }
            } else {
                ssd1306_draw_string(0, 16, "MODE: USB CONSOLE");
            }

            // Line 3: Display your active synchronization state
            extern bool wave_engine_running;
            if (wave_engine_running) {
                ssd1306_draw_string(0, 32, "STATUS: RUNNING");
            } else {
                ssd1306_draw_string(0, 32, "STATUS: W_SYNC [GP28]");
            }

            // Push data from your Pico RAM buffer out to the physical glass panel
            ssd1306_show(); 
        }		
		
        // ====================================================================
        // EXCLUSIVE POLLING ALLOCATION ENGINE
        // Alternates entirely based on your layout boolean selection
        // ====================================================================
		
        if (use_network_interface) {
            process_network_loop(); // Dedicated entirely to Ethernet Port 5000
        } else {
            process_usb_console_loop(); // Dedicated entirely to USB serial terminals
        }

    }

    return 0;
}

