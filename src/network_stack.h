//network_stack.h
/*
#ifndef NETWORK_STACK_H
#define NETWORK_STACK_H

#include "pico/stdlib.h"

void init_network_dhcp(void);
void process_network_loop(void);
bool get_ip_assigned_status(char* ip_buffer);

#endif
*/

#ifndef NETWORK_STACK_H
#define NETWORK_STACK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Public Global Status Flags (Polled by main.c / OLED)
extern uint8_t g_dhcp_get_ip_flag;
extern uint8_t oled_dhcp_ip_flag;
extern uint8_t g_dns_get_ip_flag;
extern char current_ip_str[32];

// Public Initialization & Execution Functions
void init_network_stack(void);
void process_network_loop(void);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_STACK_H
