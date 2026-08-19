/*

*/

#ifndef WAVE_ENGINE_H
#define WAVE_ENGINE_H

#include "pico/stdlib.h"
#include "pico/sync.h"

#define SINE_CHANNELS 6	//(GP 4-9), was 12 (GP 4-15)
#define LUT_SIZE 256
#define PWM_WRAP 4999
#define TIMER_INTERVAL_US 65

typedef struct {
    uint gpio;
    uint pwm_slice;
    uint pwm_channel;
    float amplitude;
    uint32_t phase_shift;     // Explicit base shift offset index (0 to 255)
    uint32_t phase_accumulator; // FIX: Transitioned to 32-bit (24.8 fixed-point format)
    bool is_enabled;
} SineWaveChannel;

extern SineWaveChannel channels[SINE_CHANNELS];
extern critical_section_t wave_crit_sec;
// Expose the flag safely to Core 0
//extern volatile bool wave_engine_running;

// FIX: New global runtime frequency tuning parameters
extern float global_frequency;
extern uint32_t global_phase_increment; // Step size tracking scaling fractional parts

extern float current_system_freq_hz;

void init_wave_engine(void);
void core1_entry(void);
void update_system_frequency(float hz); // Helper function called by AT command parser

#endif

