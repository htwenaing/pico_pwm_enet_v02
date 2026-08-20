//src/at_commands.c
#include "at_commands.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

// Underlying system component connections
#include "wave_engine.h"
#include "i2c_expanders.h"
#include "socket.h" // Wiznet raw socket APIs for send()
//test
//#include "network_stack.h"

#include "hardware/uart.h"

#define BRIDGE_UART uart0

#define SOCKET_CMD_TCP 2	//

// Helper function to send responses back to the active connection channel
static void respond_to_client(const char* message, bool is_network_source) {
    if (is_network_source) {
        // Send down the active TCP channel
        send(SOCKET_CMD_TCP, (uint8_t*)message, strlen(message));
    } else {
        // Print directly back to the active serial interface
        printf("%s", message);
        fflush(stdout);
    }
}

void execute_unified_at_command(const char* cmd, bool is_network_source) {
    extern critical_section_t wave_crit_sec;
    char response_buffer[128];
	
    // ====================================================================
    // 1. ROUTER INTERCEPT: TARGET PICO B (SLAVE NODE)
    // If Pico A receives "AT2+", bypass all local processing and forward to UART.
    // ====================================================================
    if (strncmp(cmd, "AT2+", 4) == 0) {
        if (is_network_source) {
            // Forward translated or raw command over physical UART to Pico B
            // Option: convert "AT2+PWM=..." -> "AT+PWM=..." so Pico B uses standard syntax
            uart_puts(BRIDGE_UART, "AT+");
            uart_puts(BRIDGE_UART, cmd + 4); // Skips "AT2+" and appends the rest
            uart_puts(BRIDGE_UART, "\r\n");
        }
        // Local engine function on Pico A is EMPTY — does nothing!
        return; 
    }	

    // 1. Sine Settings: AT+PWM=<channel>,<amplitude>,<phase>,<enable>
    if (strncmp(cmd, "AT+PWM=", 7) == 0) {
        int ch_num, phase_deg, enabled_state;
        float amp;
        if (sscanf(cmd, "AT+PWM=%d,%f,%d,%d", &ch_num, &amp, &phase_deg, &enabled_state) == 4) {
            if (ch_num < 1 || ch_num > 6) {
                respond_to_client("ERROR: SINE CHANNEL RANGE (1-6)\r\n", is_network_source);
                return;
            }
            uint32_t final_phase_idx = (uint32_t)(((float)phase_deg / 360.0f) * LUT_SIZE) % LUT_SIZE;

            critical_section_enter_blocking(&wave_crit_sec);
            channels[ch_num - 1].amplitude = (amp < 0.0f) ? 0.0f : ((amp > 1.0f) ? 1.0f : amp);
            channels[ch_num - 1].phase_shift = final_phase_idx;
            channels[ch_num - 1].is_enabled = (enabled_state == 1);
            critical_section_exit(&wave_crit_sec);

            snprintf(response_buffer, sizeof(response_buffer), "OK: SINE CH %d SET\r\n", ch_num);
            respond_to_client(response_buffer, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID PWM SYNTAX\r\n", is_network_source);
        }
    } 
    // 2. Frequency Modulation: AT+FREQ=<value>
    else if (strncmp(cmd, "AT+FREQ=", 8) == 0) {
        float target_hz;
        if (sscanf(cmd, "AT+FREQ=%f", &target_hz) == 1) {
            if (target_hz < 40.0f || target_hz > 70.0f) {
                respond_to_client("ERROR: FREQUENCY OUT OF RANGE (40-70HZ)\r\n", is_network_source);
                return;
            }
            update_system_frequency(target_hz);
            snprintf(response_buffer, sizeof(response_buffer), "OK: FREQUENCY CHANGED TO %.2f HZ\r\n", target_hz);
            respond_to_client(response_buffer, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID FREQUENCY SYNTAX\r\n", is_network_source);
        }
    }
    // 3. PCA9685 Config: AT+PCA=<dev_idx>,<channel>,<duty>
    else if (strncmp(cmd, "AT+PCA=", 7) == 0) {
        int dev_idx, ch, duty;
        if (sscanf(cmd, "AT+PCA=%d,%d,%d", &dev_idx, &ch, &duty) == 3) {
            if (dev_idx < 0 || dev_idx > 3 || ch < 0 || ch > 15 || duty < 0 || duty > 4095) {
                respond_to_client("ERROR: INVALID PCA PARAMETERS\r\n", is_network_source);
                return;
            }
            uint8_t target_addr = PCA9685_BASE_ADDR + dev_idx;
            pca9685_set_pwm(target_addr, ch, duty);
            snprintf(response_buffer, sizeof(response_buffer), "OK: PCA DEVI%d CH%d SET TO %d\r\n", dev_idx, ch, duty);
            respond_to_client(response_buffer, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID PCA SYNTAX\r\n", is_network_source);
        }
    }
    // 4. MCP23017 Toggle: AT+MCP=<dev_idx>,<pin>,<state>
    else if (strncmp(cmd, "AT+MCP=", 7) == 0) {
        int dev_idx, pin, state;
        if (sscanf(cmd, "AT+MCP=%d,%d,%d", &dev_idx, &pin, &state) == 3) {
            if (dev_idx < 0 || dev_idx > 3 || pin < 0 || pin > 15 || (state != 0 && state != 1)) {
                respond_to_client("ERROR: INVALID MCP PARAMETERS\r\n", is_network_source);
                return;
            }
            uint8_t target_addr = MCP23017_BASE_ADDR + dev_idx;
            mcp23017_write_gpio(target_addr, pin, state);
            snprintf(response_buffer, sizeof(response_buffer), "OK: MCP DEVI%d PIN%d SET TO %d\r\n", dev_idx, pin, state);
            respond_to_client(response_buffer, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID MCP SYNTAX\r\n", is_network_source);
        }
    }
    // 5. Hardware Sync Bypass String: AT+SYNCBYPASS
    else if (strcmp(cmd, "AT+SYNCBYPASS") == 0) {
        extern bool wave_engine_running;
        critical_section_enter_blocking(&wave_crit_sec);
        if (wave_engine_running) {
            critical_section_exit(&wave_crit_sec);
            respond_to_client("WARN: WAVE ENGINE IS ALREADY RUNNING\r\n", is_network_source);
        } else {
            wave_engine_running = true;
            critical_section_exit(&wave_crit_sec);
            respond_to_client("OK: HARDWARE SYNC BYPASS EXECUTED\r\n", is_network_source);
        }
    }
    // 6. Toggle Hardware Synchronization Mode: AT+SYNCMODE=<0 or 1>
    else if (strncmp(cmd, "AT+SYNCMODE=", 12) == 0) {
        int setting;
        if (sscanf(cmd, "AT+SYNCMODE=%d", &setting) == 1) {
            extern volatile bool use_hardware_sync;
            critical_section_enter_blocking(&wave_crit_sec);
            use_hardware_sync = (setting == 1);
            critical_section_exit(&wave_crit_sec);
            respond_to_client(setting == 1 ? "OK: SYNC ACTIVE\r\n" : "OK: SYNC BYPASSED\r\n", is_network_source);
        }
    }
    // 7. Comprehensive System Status Query: AT+STATUS
    //else if (strcmp(cmd, "AT+STATUS") == 0) {	//doesn't work
    // 7A. Core System Status Query: AT+STATUS
    else if (strncmp(cmd, "AT+STATUS", 9) == 0) {
        extern bool wave_engine_running;
        extern volatile bool use_hardware_sync;
        extern volatile bool use_network_interface;
        extern char current_ip_str[32];
        extern float current_system_freq_hz;

        char report[256]; // Kept comfortably small 

        snprintf(report, sizeof(report),
            "\r\n=== WAVE GENERATOR SYSTEM STATUS ===\r\n"
            "Active Mode       : %s\r\n"
            "IP Address        : %s\r\n"
            "Hardware Sync Pin : GP28 (%s)\r\n"
            "Wave Engine State : %s\r\n"
            "System Frequency  : %.2f Hz\r\n"
            "====================================\r\n",
            use_network_interface ? "Ethernet (Port 5000)" : "USB Virtual COM",
            current_ip_str,
            use_hardware_sync ? "ENABLED" : "BYPASSED",
            wave_engine_running ? "RUNNING" : "WAITING_SYNC [GP28]",
            current_system_freq_hz
        );

        respond_to_client(report, is_network_source);
    }
    // 7B. Single Channel Query: AT+CH1? through AT+CH6?
    else if (strncmp(cmd, "AT+CH", 5) == 0 && (cmd[5] >= '1' && cmd[5] <= '6') && cmd[6] == '?') {
        int ch_idx = (cmd[5] - '1'); // Maps '1'-'6' directly to index 0-5
        
        // Fast, isolated snapshot of just ONE channel under critical section
        float snap_amp;
        uint32_t snap_phase;
        bool snap_en;

        critical_section_enter_blocking(&wave_crit_sec);
        snap_amp   = channels[ch_idx].amplitude;
        snap_phase = channels[ch_idx].phase_shift;
        snap_en    = channels[ch_idx].is_enabled;
        critical_section_exit(&wave_crit_sec);

        // Calculate phase safely outside the critical section
        float phase_deg = 0.0f;
        if (LUT_SIZE > 0) {
            phase_deg = ((float)snap_phase / (float)LUT_SIZE) * 360.0f;
        }

        // Tiny buffer: Uses only 80 bytes of stack memory
        char report[80];
        snprintf(report, sizeof(report),
            "OK: CH%d [En:%d] Amp:%.2f Phase:%.1f deg\r\n",
            ch_idx + 1,
            snap_en ? 1 : 0,
            snap_amp,
            phase_deg
        );

        respond_to_client(report, is_network_source);
    }
    // 8. Group Amplitude Control: AT1+VIV=<amplitude>
    // Sets Channels 1, 2, and 3 (array index 0, 1, 2) amplitude simultaneously
    else if (strncmp(cmd, "AT1+VIV=", 8) == 0) {
        float amp;
        if (sscanf(cmd, "AT1+VIV=%f", &amp) == 1) {
            // Clamp amplitude between 0.0 and 1.0 safely
            float clamped_amp = (amp < 0.0f) ? 0.0f : ((amp > 1.0f) ? 1.0f : amp);

            critical_section_enter_blocking(&wave_crit_sec);
            channels[0].amplitude = clamped_amp; // Channel 1
            channels[1].amplitude = clamped_amp; // Channel 2
            channels[2].amplitude = clamped_amp; // Channel 3
            critical_section_exit(&wave_crit_sec);

            char response[64];
            snprintf(response, sizeof(response), "OK: CH1-3 AMP SET TO %.2f\r\n", clamped_amp);
            respond_to_client(response, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID VIV SYNTAX (e.g. AT1+VIV=0.5)\r\n", is_network_source);
        }
    }
    // 9. Group Phase Control: AT1+VIP=<ch1_deg>,<ch2_deg>,<ch3_deg>
    // Adjusts Phase shifts for Channels 1, 2, and 3 (array indexes 0, 1, 2) simultaneously
    else if (strncmp(cmd, "AT1+VIP=", 8) == 0) {
        int phase1, phase2, phase3;
        if (sscanf(cmd, "AT1+VIP=%d,%d,%d", &phase1, &phase2, &phase3) == 3) {
            
            // Defend against uninitialized or zero LUT boundaries
            if (LUT_SIZE == 0) {
                respond_to_client("ERROR: WAVE ENGINE UNINITIALIZED\r\n", is_network_source);
                return;
            }

            // Pre-calculate Look-Up Table target index mappings safely
            uint32_t idx1 = (uint32_t)(((float)phase1 / 360.0f) * LUT_SIZE) % LUT_SIZE;
            uint32_t idx2 = (uint32_t)(((float)phase2 / 360.0f) * LUT_SIZE) % LUT_SIZE;
            uint32_t idx3 = (uint32_t)(((float)phase3 / 360.0f) * LUT_SIZE) % LUT_SIZE;

            // Microscopic critical section access window
            critical_section_enter_blocking(&wave_crit_sec);
            channels[0].phase_shift = idx1; // Channel 1
            channels[1].phase_shift = idx2; // Channel 2
            channels[2].phase_shift = idx3; // Channel 3
            critical_section_exit(&wave_crit_sec);

            char response[64];
            snprintf(response, sizeof(response), "OK: CH1-3 PHASES SET (%d,%d,%d deg)\r\n", phase1, phase2, phase3);
            respond_to_client(response, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID VIP SYNTAX (e.g., AT1+VIP=0,120,240)\r\n", is_network_source);
        }
    }
    // 10. Group Enable Control: AT1+VIE=<0 or 1>
    // Enables or disables Channels 1, 2, and 3 simultaneously
    else if (strncmp(cmd, "AT1+VIE=", 8) == 0) {
        int state;
        if (sscanf(cmd, "AT1+VIE=%d", &state) == 1) {
            if (state != 0 && state != 1) {
                respond_to_client("ERROR: INVALID VIE STATE (MUST BE 0 OR 1)\r\n", is_network_source);
                return;
            }

            bool enable_flag = (state == 1);

            critical_section_enter_blocking(&wave_crit_sec);
            channels[0].is_enabled = enable_flag; // Channel 1
            channels[1].is_enabled = enable_flag; // Channel 2
            channels[2].is_enabled = enable_flag; // Channel 3
            critical_section_exit(&wave_crit_sec);

            char response[64];
            snprintf(response, sizeof(response), "OK: CH1-3 STATE SET TO %s\r\n", enable_flag ? "ENABLED" : "DISABLED");
            respond_to_client(response, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID VIE SYNTAX (e.g., AT1+VIE=1)\r\n", is_network_source);
        }
    }
    // 11. Group 2 Amplitude Control: AT1+III=<amplitude>
    // Sets Channels 4, 5, and 6 (array index 3, 4, 5) amplitude simultaneously
    else if (strncmp(cmd, "AT1+III=", 8) == 0) {
        float amp;
        if (sscanf(cmd, "AT1+III=%f", &amp) == 1) {
            float clamped_amp = (amp < 0.0f) ? 0.0f : ((amp > 1.0f) ? 1.0f : amp);

            critical_section_enter_blocking(&wave_crit_sec);
            channels[3].amplitude = clamped_amp; // Channel 4
            channels[4].amplitude = clamped_amp; // Channel 5
            channels[5].amplitude = clamped_amp; // Channel 6
            critical_section_exit(&wave_crit_sec);

            char response[64];
            snprintf(response, sizeof(response), "OK: CH4-6 AMP SET TO %.2f\r\n", clamped_amp);
            respond_to_client(response, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID III SYNTAX (e.g., AT1+III=0.75)\r\n", is_network_source);
        }
    }

    // 12. Group 2 Phase Control: AT1+IIP=<ch4_deg>,<ch5_deg>,<ch6_deg>
    // Adjusts Phase shifts for Channels 4, 5, and 6 (array indexes 3, 4, 5) simultaneously
    else if (strncmp(cmd, "AT1+IIP=", 8) == 0) {
        int phase4, phase5, phase6;
        if (sscanf(cmd, "AT1+IIP=%d,%d,%d", &phase4, &phase5, &phase6) == 3) {
            if (LUT_SIZE == 0) {
                respond_to_client("ERROR: WAVE ENGINE UNINITIALIZED\r\n", is_network_source);
                return;
            }

            uint32_t idx4 = (uint32_t)(((float)phase4 / 360.0f) * LUT_SIZE) % LUT_SIZE;
            uint32_t idx5 = (uint32_t)(((float)phase5 / 360.0f) * LUT_SIZE) % LUT_SIZE;
            uint32_t idx6 = (uint32_t)(((float)phase6 / 360.0f) * LUT_SIZE) % LUT_SIZE;

            critical_section_enter_blocking(&wave_crit_sec);
            channels[3].phase_shift = idx4; // Channel 4
            channels[4].phase_shift = idx5; // Channel 5
            channels[5].phase_shift = idx6; // Channel 6
            critical_section_exit(&wave_crit_sec);

            char response[128];
            snprintf(response, sizeof(response), "OK: CH4-6 PHASES SET (%d,%d,%d deg)\r\n", phase4, phase5, phase6);
            respond_to_client(response, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID IIP SYNTAX (e.g., AT1+IIP=0,120,240)\r\n", is_network_source);
        }
    }

    // 13. Group 2 Enable Control: AT1+IIE=<0 or 1>
    // Enables or disables Channels 4, 5, and 6 simultaneously
    else if (strncmp(cmd, "AT1+IIE=", 8) == 0) {
        int state;
        if (sscanf(cmd, "AT1+IIE=%d", &state) == 1) {
            if (state != 0 && state != 1) {
                respond_to_client("ERROR: INVALID IIE STATE (MUST BE 0 OR 1)\r\n", is_network_source);
                return;
            }

            bool enable_flag = (state == 1);

            critical_section_enter_blocking(&wave_crit_sec);
            channels[3].is_enabled = enable_flag; // Channel 4
            channels[4].is_enabled = enable_flag; // Channel 5
            channels[5].is_enabled = enable_flag; // Channel 6
            critical_section_exit(&wave_crit_sec);

            char response[64];
            snprintf(response, sizeof(response), "OK: CH4-6 STATE SET TO %s\r\n", enable_flag ? "ENABLED" : "DISABLED");
            respond_to_client(response, is_network_source);
        } else {
            respond_to_client("ERROR: INVALID IIE SYNTAX (e.g., AT1+IIE=1)\r\n", is_network_source);
        }
    }

	
	// new cmd here
    else {
        respond_to_client("ERROR: UNKNOWN SYNTAX\r\n", is_network_source);
    }
}


/*
Do not worry—your wave engine hasn't actually crashed! Your code is hitting a classic variable sizing bug called a stack buffer overflow, which is accidentally erasing Core 0's memory and corrupting your system state. [1] 
Let's look at how the AT+STATUS response buffer was declared:

char report[256]; // 256 bytes allocated on the stack

Because the system status string contains a massive diagnostic table (header info, system frequency, network config, plus 6 distinct channels of amplitude and phase data), the actual length of the generated string is well over 300 characters.
When snprintf writes past the 256th character, it overflows the bounds of the report array and starts overwriting critical system memory blocks right next to it. This memory corruption instantly breaks the loop pointers, crashes Core 0, stops your heartbeat LED, and halts data coordination with Core 1.
## The Fix: Increase and Guard the Buffer Memory
Open your src/at_commands.c file, look inside the AT+STATUS command block, and increase the size of the report stack array to 512 bytes.
Change this line:

// Change this line inside the AT+STATUS block:char report[512]; // Increased to 512 bytes to safely fit all data charactersint offset = 0;

## Why this fixes the problem completely:

* Adequate Breathing Room: 512 bytes provides plenty of headroom to store the complete 6-channel text dashboard without clipping.
* Built-in snprintf Protection: Because your code utilizes sizeof(report) - offset, it will dynamically truncate the message if it ever hits the 512-byte wall, rather than overflowing into other memory blocks and crashing the processor.

Save the change, recompile, and flash your code. Your AT+STATUS diagnostics will now stream perfectly, your heartbeat LED will stay alive, and you can safely head home!
Let me know if the status dashboard dumps smoothly without halting your system loop.

[1] [https://windowsforum.com](https://windowsforum.com/security-alerts.84/cve-2025-26688-understanding-and-mitigating-a-critical-windows-vulnerability.359965/)


Command Map Summary
Command Sent over TCP (Port 5000)	Handled by Pico A (Gateway)	Forwarded to Pico B (Node)	Action Taken
AT+PWM=... / AT1+PWM=...			Yes (Locally executed)		No							Modifies Pico A waveforms
AT2+PWM=...							No (Empty function return)	Yes (Transmitted over UART)	Modifies Pico B waveforms
AT+STATUS / AT1+STATUS				Yes (Locally executed)		No							Returns Pico A telemetry
AT2+STATUS							No (Empty function return)	Yes (Transmitted over UART)	Returns Pico B telemetry

*/