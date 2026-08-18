//src/usb_console.c

/*
The code provided for usb_console.c is written specifically for USB Standard I/O (USB CDC) using the Pico SDK's generic stdio functions (stdio_init_all(), getchar_timeout_us(), putchar()).
Whether it runs over USB, physical UART pins, or both simultaneously depends entirely on your settings inside CMakeLists.txt.
Here is how you control which physical interface the code communicates through:
------------------------------
## Scenario A: Run over USB Only (Current Setup)
If your CMakeLists.txt is configured like this:

pico_enable_stdio_usb(pico_sine_ethernet 1)
pico_enable_stdio_uart(pico_sine_ethernet 0)


* Result: The code will listen and speak only through the USB cable plugged into your computer as a virtual COM port.

------------------------------
## Scenario B: Run over Physical UART Only (GP0/GP1)
If your CMakeLists.txt is configured like this:

pico_enable_stdio_usb(pico_sine_ethernet 0)
pico_enable_stdio_uart(pico_sine_ethernet 1)


* Result: The exact same usb_console.c code changes its routing completely! It will stop using the USB port entirely and will instead listen and speak only through physical pins GP0 (TX) and GP1 (RX) at 115200 baud.

------------------------------
## Scenario C: Run over BOTH USB and UART Simultaneously 🚀
One of the most powerful features of the Pico SDK is that you can set both flags to 1 at the same time:

pico_enable_stdio_usb(pico_sine_ethernet 1)
pico_enable_stdio_uart(pico_sine_ethernet 1)


* Result: The Pico SDK binds them together under the hood.
* Data Out (printf / putchar): Any text your Pico prints will be duplicated and sent out of both the USB cable and the physical GP0 pin at the exact same time.
* Data In (getchar_timeout_us): The code will actively listen to both interfaces. You can type an AT command into your USB terminal window, or you can type it into a serial device wired to GP1, and the console code will process it seamlessly.

## ⚠️ A Warning About Scenario C (Running Both)
If you enable both interfaces, getchar_timeout_us(0) checks the unified input buffer. If no characters are arriving on either port, it returns PICO_ERROR_TIMEOUT. However, if you are actively typing a command on the USB terminal, a device connected to the physical UART RX pin cannot inject letters in the middle of your word without scrambling the input buffer string.
For bench testing and debugging, setting both flags to 1 is highly recommended because it gives you total flexibility without needing to modify your C code.
Let me know if you would like to proceed with setting up simultaneous dual-console support, or if you want to lock the project strictly into a single interface!

The baud rate for this usb_console code depends on which interface is active based on your CMakeLists.txt settings:
## 1. When running over the USB Cable (pico_enable_stdio_usb = 1)

* Baud Rate: Any baud rate works (It is virtual).
* Why: When communicating over a native USB cable (USB CDC), the connection is a purely virtual serial port running at the absolute maximum speed of the USB hardware layer. Your terminal emulator software (like PuTTY or Tera Term) can be set to 115200, 9600, or 921600—it will connect and transfer data perfectly regardless of the setting, as the baud rate configuration is ignored by the USB controller. [1, 2, 3, 4, 5] 

## 2. When running over physical pins GP0/GP1 (pico_enable_stdio_uart = 1)

* Baud Rate: 115200 bps (Standard Pico SDK default).
* Why: When you activate pico_enable_stdio_uart(pico_sine_ethernet 1), the Pico SDK's underlying architecture initializes uart0 with a hardcoded baseline frequency profile of exactly 115200 baud, using 8 data bits, no parity, and 1 stop bit (8N1).

------------------------------
## Pro-Tip: How to change the physical UART baud rate
If you have both USB and UART enabled, but your external physical serial device requires a different speed than 115200 (such as 9600 or 57600), you can override the default speed right after initializing the system. [6, 7] 
Inside your usb_console.c file, update your init_usb_console block to explicitly change the hardware clock routing like this:

void init_usb_console(void) {
    // 1. Initialize universal SDK stdio routing (handles both USB and UART)
    stdio_init_all();
    
    // 2. Explicitly override the physical UART speed if pins are enabled in CMake
    // Changes hardware UART0 to your desired speed (e.g., 9600, 57600, etc.)
    uart_set_baudrate(uart0, 115200); 
}

If you are just plugging a standard USB cable directly from the Pico into your computer, you can leave everything as-is, set your computer's terminal to 115200, and it will work perfectly. [8] 
Let me know if your serial hardware setup requires a specific custom baud profile, or if you're ready to proceed with checking the W5100S networking logic loops!

*/


#include "usb_console.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

// Bring in your project's engine headers
#include "wave_engine.h"
#include "i2c_expanders.h"

#define CMD_BUFFER_SIZE  128
static char uart_rx_buffer[CMD_BUFFER_SIZE];
static int buffer_idx = 0;

// Internal helper to parse commands once a newline is received
static void parse_uart_at_command(const char* cmd) {
    extern critical_section_t wave_crit_sec;
    
    // 1. Sine Wave Settings: AT+PWM=<channel>,<amplitude>,<phase>,<enable>
    if (strncmp(cmd, "AT+PWM=", 7) == 0) {
        int ch_num, phase_deg, enabled_state;
        float amp;
        if (sscanf(cmd, "AT+PWM=%d,%f,%d,%d", &ch_num, &amp, &phase_deg, &enabled_state) == 4) {
            if (ch_num < 1 || ch_num > 6) {
                printf("ERROR: SINE CHANNEL RANGE (1-6)\r\n");
                return;
            }
            uint32_t final_phase_idx = (uint32_t)(((float)phase_deg / 360.0f) * LUT_SIZE) % LUT_SIZE;

            critical_section_enter_blocking(&wave_crit_sec);
            channels[ch_num - 1].amplitude = (amp < 0.0f) ? 0.0f : ((amp > 1.0f) ? 1.0f : amp);
            channels[ch_num - 1].phase_shift = final_phase_idx;
            channels[ch_num - 1].is_enabled = (enabled_state == 1);
            critical_section_exit(&wave_crit_sec);

            printf("OK: SINE CH %d SET\r\n", ch_num);
        } else {
            printf("ERROR: INVALID PWM SYNTAX\r\n");
        }
    } 
    // 2. Dynamic Frequency Modulation: AT+FREQ=<value>
    else if (strncmp(cmd, "AT+FREQ=", 8) == 0) {
        float target_hz;
        if (sscanf(cmd, "AT+FREQ=%f", &target_hz) == 1) {
            if (target_hz < 40.0f || target_hz > 70.0f) {
                printf("ERROR: FREQUENCY OUT OF RANGE (40-70HZ)\r\n");
                return;
            }
            update_system_frequency(target_hz);
            printf("OK: FREQUENCY CHANGED TO %.2f HZ\r\n", target_hz);
        } else {
            printf("ERROR: INVALID FREQUENCY SYNTAX\r\n");
        }
    }
    // 3. PCA9685 Control String: AT+PCA=<dev_idx>,<channel>,<duty>
    else if (strncmp(cmd, "AT+PCA=", 7) == 0) {
        int dev_idx, ch, duty;
        if (sscanf(cmd, "AT+PCA=%d,%d,%d", &dev_idx, &ch, &duty) == 3) {
            if (dev_idx < 0 || dev_idx > 3 || ch < 0 || ch > 15 || duty < 0 || duty > 4095) {
                printf("ERROR: INVALID PCA PARAMETERS\r\n");
                return;
            }
            uint8_t target_addr = PCA9685_BASE_ADDR + dev_idx;
            pca9685_set_pwm(target_addr, ch, duty);
            printf("OK: PCA DEVI%d CH%d SET TO %d\r\n", dev_idx, ch, duty);
        } else {
            printf("ERROR: INVALID PCA SYNTAX\r\n");
        }
    }
    // 4. MCP23017 Control String: AT+MCP=<dev_idx>,<pin>,<state>
    else if (strncmp(cmd, "AT+MCP=", 7) == 0) {
        int dev_idx, pin, state;
        if (sscanf(cmd, "AT+MCP=%d,%d,%d", &dev_idx, &pin, &state) == 3) {
            if (dev_idx < 0 || dev_idx > 3 || pin < 0 || pin > 15 || (state != 0 && state != 1)) {
                printf("ERROR: INVALID MCP PARAMETERS\r\n");
                return;
            }
            uint8_t target_addr = MCP23017_BASE_ADDR + dev_idx;
            mcp23017_write_gpio(target_addr, pin, state);
            printf("OK: MCP DEVI%d PIN%d SET TO %d\r\n", dev_idx, pin, state);
        } else {
            printf("ERROR: INVALID MCP SYNTAX\r\n");
        }
    }
    // 5. Hardware Sync Bypass String: AT+SYNCBYPASS
    else if (strcmp(cmd, "AT+SYNCBYPASS") == 0) {
        extern bool wave_engine_running;
        critical_section_enter_blocking(&wave_crit_sec);
        if (wave_engine_running) {
            critical_section_exit(&wave_crit_sec);
            printf("WARN: WAVE ENGINE IS ALREADY RUNNING\r\n");
        } else {
            wave_engine_running = true;
            critical_section_exit(&wave_crit_sec);
            printf("OK: HARDWARE SYNC BYPASS EXECUTED. SINE WAVE ENGINE STARTED!\r\n");
        }
    }
    else {
        printf("ERROR: UNKNOWN SYNTAX\r\n");
    }
}

void init_usb_console(void) {
    // 1. Initialize universal SDK stdio routing (handles both USB and UART)
    stdio_init_all();
    
    // 2. Explicitly override the physical UART speed if pins are enabled in CMake
    // Changes hardware UART0 to your desired speed (e.g., 9600, 57600, etc.)
    uart_set_baudrate(uart0, 115200); 
}


void process_usb_console_loop(void) {
    int ch = getchar_timeout_us(0);
    
    while (ch != PICO_ERROR_TIMEOUT) {
        if (ch == '\b' || ch == 127) {
            if (buffer_idx > 0) {
                buffer_idx--;
                printf("\b \b"); 
                fflush(stdout);
            }
            ch = getchar_timeout_us(0);
            continue;
        }

        putchar(ch);
        fflush(stdout);
        
        if (ch == '\r' || ch == '\n') {
            if (buffer_idx > 0) {
                uart_rx_buffer[buffer_idx] = '\0';
                printf("\n");
                parse_uart_at_command(uart_rx_buffer);
                buffer_idx = 0;
                printf("PICO> ");
                fflush(stdout);
            }
        } else if (buffer_idx < (CMD_BUFFER_SIZE - 1)) {
            uart_rx_buffer[buffer_idx++] = ch;
        }
        
        ch = getchar_timeout_us(0);
    }
}
