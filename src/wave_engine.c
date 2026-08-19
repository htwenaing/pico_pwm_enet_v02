//
/*
to skip SYNC_INPUT_PIN, comment out, line 167 to 175
*/

#include "wave_engine.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "pico/sync.h"
#include <math.h>

#define ZERO_CROSS_PIN 22
#define SYNC_INPUT_PIN 28 // One-time synchronization input
#define REF1_PWM_PIN   14 // was 10, Auto-Tracking Reference 1 (Tracks Channel 1 / Index 0)
#define REF2_PWM_PIN   15 // was 11, Auto-Tracking Reference 2 (Tracks Channel 4 / Index 3)

// Shared global flag to let Core 0 read Core 1's state
volatile bool wave_engine_running = false; 

SineWaveChannel channels[SINE_CHANNELS];
uint32_t sine_lut[LUT_SIZE];
critical_section_t wave_crit_sec;

// Global hardware slice/channel variables for GP10 Reference 1
uint ref1_pwm_slice;
uint ref1_pwm_chan;

// Global hardware slice/channel variables for GP11 Reference 2
uint ref2_pwm_slice;
uint ref2_pwm_chan;

// Variable frequency storage
float global_frequency = 60.0f;
uint32_t global_phase_increment = 0;

float current_system_freq_hz = 50.0f; // Default baseline frequency (e.g. 50 Hz or 60 Hz)


// Converts target frequency into a 24.8 fixed-point tuning word step size
void update_system_frequency(float hz) {
	current_system_freq_hz = hz;
    if (hz < 40.0f) hz = 40.0f;
    if (hz > 70.0f) hz = 70.0f;
    
    critical_section_enter_blocking(&wave_crit_sec);
    global_frequency = hz;
    // Step size formula: (Hz * LUT_Size * Interrupt_Period_Seconds) * 256 scaling bits
    float steps_per_interrupt = hz * (float)LUT_SIZE * ((float)TIMER_INTERVAL_US / 1000000.0f);
    global_phase_increment = (uint32_t)(steps_per_interrupt * 256.0f); // Store as shift scaled fixed-point
    critical_section_exit(&wave_crit_sec);
}

void generate_sine_lut(void) {
    for (int i = 0; i < LUT_SIZE; i++) {
        double radians = (i * 2.0 * M_PI) / LUT_SIZE;
        sine_lut[i] = (uint32_t)(((sin(radians) + 1.0) / 2.0) * PWM_WRAP);
    }
}

bool repeating_timer_callback(struct repeating_timer *t) {
    critical_section_enter_blocking(&wave_crit_sec);

    uint32_t old_accum = channels[0].phase_accumulator;

    // 1. Process the 6 Active Sine Wave Channels
    for (int i = 0; i < SINE_CHANNELS; i++) {
        // Step forward by the dynamic fractional tuning word instead of a flat 1.0f
        channels[i].phase_accumulator += global_phase_increment;
        
        // Wrap around boundary limit for 256-point LUT with 8 fractional bits (256 * 256 = 65536)
        if (channels[i].phase_accumulator >= (LUT_SIZE << 8)) {
            channels[i].phase_accumulator -= (LUT_SIZE << 8);
        }

        if (!channels[i].is_enabled) {
            pwm_set_chan_level(channels[i].pwm_slice, channels[i].pwm_channel, 0);
            continue;
        }

        // Drop fractional bits via shift to get the real integer table index
        uint32_t base_idx = channels[i].phase_accumulator >> 8;
        uint32_t current_idx = (base_idx + channels[i].phase_shift) % LUT_SIZE;
        
        uint32_t final_duty = (uint32_t)((float)sine_lut[current_idx] * channels[i].amplitude);
        pwm_set_chan_level(channels[i].pwm_slice, channels[i].pwm_channel, final_duty);
    }

    // 2. Auto-Tracking Reference PWM 1 Update (Tracks Channel 1 / Index 0 Amplitude)
    // Target Duty Cycle = 3/8 * amp = 0.375 * amp
    uint32_t ref1_cc = (uint32_t)(0.375f * channels[0].amplitude * (float)PWM_WRAP);
    if (ref1_cc > PWM_WRAP) ref1_cc = PWM_WRAP;
    pwm_set_chan_level(ref1_pwm_slice, ref1_pwm_chan, ref1_cc);

    // 3. Auto-Tracking Reference PWM 2 Update (Tracks Channel 4 / Index 3 Amplitude)
    // Target Duty Cycle = 3/8 * amp = 0.375 * amp
    uint32_t ref2_cc = (uint32_t)(0.375f * channels[3].amplitude * (float)PWM_WRAP);
    if (ref2_cc > PWM_WRAP) ref2_cc = PWM_WRAP;
    pwm_set_chan_level(ref2_pwm_slice, ref2_pwm_chan, ref2_cc);

    // 4. Zero-Crossing Logic: Evaluates changes on integer index step boundary shifts
    if ((channels[0].phase_accumulator >> 8) < (old_accum >> 8)) {
        gpio_put(ZERO_CROSS_PIN, 1);
    } else if ((channels[0].phase_accumulator >> 8) > 10) {
        gpio_put(ZERO_CROSS_PIN, 0);
    }

    critical_section_exit(&wave_crit_sec);
    return true;
}

void init_wave_engine(void) {
    wave_engine_running = false; 
    generate_sine_lut();
    critical_section_init(&wave_crit_sec);
    update_system_frequency(60.0f); 

    // Configure Output Hardware Sync Pin
    gpio_init(ZERO_CROSS_PIN);
    gpio_set_dir(ZERO_CROSS_PIN, GPIO_OUT);
    gpio_put(ZERO_CROSS_PIN, 0);

    // Configure Input Hardware Sync Pin (GP28)
    gpio_init(SYNC_INPUT_PIN);
    gpio_set_dir(SYNC_INPUT_PIN, GPIO_IN);
    gpio_pull_down(SYNC_INPUT_PIN); 

    // Re-usable base PWM configuration block
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 1.0f);
    pwm_config_set_wrap(&config, PWM_WRAP);

    // Configure Auto-Tracking Reference PWM 1 Engine (GP10)
    gpio_set_function(REF1_PWM_PIN, GPIO_FUNC_PWM);
    ref1_pwm_slice = pwm_gpio_to_slice_num(REF1_PWM_PIN);
    ref1_pwm_chan = pwm_gpio_to_channel(REF1_PWM_PIN);
    pwm_init(ref1_pwm_slice, &config, true);
    pwm_set_chan_level(ref1_pwm_slice, ref1_pwm_chan, 0); // Keep muted at boot up

    // Configure Auto-Tracking Reference PWM 2 Engine (GP11)
    gpio_set_function(REF2_PWM_PIN, GPIO_FUNC_PWM);
    ref2_pwm_slice = pwm_gpio_to_slice_num(REF2_PWM_PIN);
    ref2_pwm_chan = pwm_gpio_to_channel(REF2_PWM_PIN);
    pwm_init(ref2_pwm_slice, &config, true);
    pwm_set_chan_level(ref2_pwm_slice, ref2_pwm_chan, 0); // Keep muted at boot up

    // Configure the 6 Active Sine Wave Generator Pins (GP4 to GP9)
    for (int i = 0; i < SINE_CHANNELS; i++) {
        channels[i].gpio = i + 8;	//was 4
        channels[i].amplitude = 1.0f;
        channels[i].phase_accumulator = 0;
        channels[i].is_enabled = true;

        int phase_group = i % 3;
        if (phase_group == 0)      channels[i].phase_shift = 0;   
        else if (phase_group == 1) channels[i].phase_shift = 85;  
        else                       channels[i].phase_shift = 171; 

        gpio_set_function(channels[i].gpio, GPIO_FUNC_PWM);
        channels[i].pwm_slice = pwm_gpio_to_slice_num(channels[i].gpio);
        channels[i].pwm_channel = pwm_gpio_to_channel(channels[i].gpio);
        
        pwm_init(channels[i].pwm_slice, &config, true);
        pwm_set_chan_level(channels[i].pwm_slice, channels[i].pwm_channel, 0);
    }
}

/*
// Launches explicitly on Core 1 - Modified for one-time sync trap
void core1_entry(void) {
    struct repeating_timer timer;

    // Phase 1: Wait for a steady LOW state if powered up mid-pulse
    while (gpio_get(SYNC_INPUT_PIN) == 1) {
        tight_loop_contents();
    }

    // Phase 2: Block Core 1 until a clean 0 to 1 rising-edge transition hits GP28
    while (gpio_get(SYNC_INPUT_PIN) == 0) {
        tight_loop_contents();
    }

    // Phase 3: Instantaneous release. Hardware timer starts immediately.
    wave_engine_running = true; 
    add_repeating_timer_us(-65, repeating_timer_callback, NULL, &timer);

    // Phase 4: Spin infinitely. GP28 is completely ignored from this point forward.
    while (1) {
        tight_loop_contents(); 
    }
}
*/

// Launches explicitly on Core 1 - Modified for one-time sync trap with feature bypass boolean
void core1_entry(void) {
    struct repeating_timer timer;
    
    // External boolean configuration and thread protector block declarations
    extern volatile bool use_hardware_sync;
    extern critical_section_t wave_crit_sec;

    // 1. Check if Hardware Sync is enabled. If false, we skip Phases 1 and 2 entirely!
    bool sync_enabled;
    
    critical_section_enter_blocking(&wave_crit_sec);
    sync_enabled = use_hardware_sync;
    critical_section_exit(&wave_crit_sec);

    if (sync_enabled) {
        // Phase 1: Wait for a steady LOW state if powered up mid-pulse
        while (gpio_get(SYNC_INPUT_PIN) == 1) {
            tight_loop_contents();
        }

        // Phase 2: Block Core 1 until a clean 0 to 1 rising-edge transition hits GP28
        while (gpio_get(SYNC_INPUT_PIN) == 0) {
            // Also allow the software 'AT+SYNCBYPASS' command to break this loop early
            bool soft_bypass;
            critical_section_enter_blocking(&wave_crit_sec);
            soft_bypass = wave_engine_running;
            critical_section_exit(&wave_crit_sec);
            
            if (soft_bypass) {
                break; // Exit the loop if software forced it on
            }

            tight_loop_contents();
        }
    }

    // Phase 3: Instantaneous release. Hardware timer starts immediately.
    critical_section_enter_blocking(&wave_crit_sec);
    wave_engine_running = true; 
    critical_section_exit(&wave_crit_sec);
    
    add_repeating_timer_us(-65, repeating_timer_callback, NULL, &timer);

    // Phase 4: Spin infinitely. GP28 is completely ignored from this point forward.
    while (1) {
        tight_loop_contents(); 
    }
}
