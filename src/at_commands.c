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

#define SOCKET_CMD_TCP 2	//commented out for test

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
	else if (strncmp(cmd, "AT+STATUS", 9) == 0) {	
        extern bool wave_engine_running;
        extern volatile bool use_hardware_sync;
        extern volatile bool use_network_interface;
        extern char current_ip_str[32];
        extern float current_system_freq_hz; // Ensure your wave_engine tracks current frequency

        char report[512];
        int offset = 0;

        offset += snprintf(report + offset, sizeof(report) - offset,
            "\r\n=== 6-CH WAVE GENERATOR SYSTEM STATUS ===\r\n"
            "Active Mode       : %s\r\n"
            "IP Address        : %s\r\n"
            "Hardware Sync Pin : GP28 (%s)\r\n"
            "Wave Engine State : %s\r\n"
            "System Frequency  : %.2f Hz\r\n"
            "--- Channel Configurations ---\r\n",
            use_network_interface ? "Ethernet (Port 5000)" : "USB Virtual COM",
            current_ip_str,
            use_hardware_sync ? "ENABLED" : "BYPASSED",
            wave_engine_running ? "RUNNING" : "WAITING_SYNC [GP28]",
            current_system_freq_hz
        );

        // Lock critical section while safely snapshotting the 6 PWM channels
        critical_section_enter_blocking(&wave_crit_sec);
        for (int i = 0; i < 6; i++) {
            float phase_deg = ((float)channels[i].phase_shift / (float)LUT_SIZE) * 360.0f;
            offset += snprintf(report + offset, sizeof(report) - offset,
                "  CH%d: [En: %d] Amp: %.2f | Phase: %.1f deg\r\n",
                i + 1,
                channels[i].is_enabled ? 1 : 0,
                channels[i].amplitude,
                phase_deg
            );
        }
        critical_section_exit(&wave_crit_sec);

        offset += snprintf(report + offset, sizeof(report) - offset,
            "=========================================\r\n"
        );

        respond_to_client(report, is_network_source);
    }
	// new cmd here
    else {
        respond_to_client("ERROR: UNKNOWN SYNTAX\r\n", is_network_source);
    }
}
