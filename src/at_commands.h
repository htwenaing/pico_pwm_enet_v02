//src/at_commands.h
#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Master Configuration Toggles (Declared as extern to share across modules)
extern volatile bool use_network_interface; // true = Ethernet Active, false = USB Console Active

// Centralized execution functions
void execute_unified_at_command(const char* cmd, bool is_network_source);

#ifdef __cplusplus
}
#endif

#endif // AT_COMMANDS_H
