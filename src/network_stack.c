#include "network_stack.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

// Wiznet ioLibrary Driver Includes
#include "wizchip_conf.h"
#include "w5x00_spi.h"   // Leverages your working SPI initializations
#include "dhcp.h"
#include "dns.h"
#include "socket.h"
#include "cmd_tcp.h" 
#include "timer.h"       // Required for 1ms timer setup

//wiznet
#include "pico/binary_info.h"
#include "pico/critical_section.h"
//#include "pico/lock_core.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
//#include "hardware/clocks.h"

//#include "wizchip_conf.h"
#include "w5x00_spi.h"

#include "dhcp.h"
#include "dns.h"

#include "timer.h"
#include "cmd_tcp.h"


// Project Engine Hooks
#include "wave_engine.h"
#include "i2c_expanders.h"

#define PLL_SYS_KHZ             (133 * 1000)
#define ETHERNET_BUF_MAX_SIZE   (1024 * 2)
#define SOCKET_DHCP             0
#define SOCKET_DNS              1
#define SOCKET_CMD_TCP          2
#define PORT_CMD_TCP            5000

// Globals
wiz_NetInfo g_net_info = {
    .mac = {0x00, 0x0E, 0x58, 0xCF, 0xFE, 0xDE},
    .ip  = {192, 168, 11, 2},
    .sn  = {255, 255, 255, 0},
    .gw  = {192, 168, 11, 1},
    .dns = {8, 8, 8, 8},
    .dhcp = NETINFO_DHCP
};

uint8_t g_dhcp_get_ip_flag = 0;
uint8_t oled_dhcp_ip_flag  = 0;
uint8_t g_dns_get_ip_flag  = 0;
uint8_t g_cmd_tcp_flag     = 0;
char current_ip_str[32]    = "0.0.0.0";

static uint8_t g_ethernet_buf[ETHERNET_BUF_MAX_SIZE];
static uint8_t g_cmd_tcp_buf[ETHERNET_BUF_MAX_SIZE];
static uint8_t g_cmd_tcp_buf_out[ETHERNET_BUF_MAX_SIZE];

static uint8_t g_dns_target_domain[] = "www.wiznet.io";
static uint8_t g_dns_target_ip[4]    = {0, 0, 0, 0};

static volatile uint16_t g_msec_cnt = 0;
static int32_t net_retval = 0;
static bool ethernet_hardware_alive = false;

extern critical_section_t wave_crit_sec;

static void repeating_timer_callback(void) {
	/*
    g_msec_cnt++;
    if (g_msec_cnt >= 1000) {
        g_msec_cnt = 0;
        DHCP_time_handler();
        DNS_time_handler();
    }
	*/
    g_msec_cnt++;
    if (g_msec_cnt >= 1000 - 1)
    {
        g_msec_cnt = 0;
        DHCP_time_handler();
        DNS_time_handler();
    }	
	
}

static void wizchip_dhcp_assign(void) {
    getIPfromDHCP(g_net_info.ip);
    getGWfromDHCP(g_net_info.gw);
    getSNfromDHCP(g_net_info.sn);
    getDNSfromDHCP(g_net_info.dns);
    g_net_info.dhcp = NETINFO_DHCP;
	//
    network_initialize(g_net_info);
    /*
    snprintf(current_ip_str, sizeof(current_ip_str), "%d.%d.%d.%d", 
             g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
    printf("[NET] DHCP Bound IP Success: %s\n", current_ip_str);
	*/
    print_network_information(g_net_info);
    printf(" DHCP leased time : %ld seconds\n", getDHCPLeasetime());	
}

static void wizchip_dhcp_conflict(void) {
    printf("[NET] Error: IP Address Conflict Detected!\n");
}

static void set_clock_khz(void) {
    set_sys_clock_khz(PLL_SYS_KHZ, true);
    clock_configure(
        clk_peri, 0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        PLL_SYS_KHZ * 1000, PLL_SYS_KHZ * 1000
    );
}

void init_network_stack(void) {
    set_clock_khz();
    
    // CRITICAL: Force use of your older working initialization routines
    wizchip_spi_initialize();
    wizchip_cris_initialize();

    wizchip_reset();
    wizchip_initialize();
	//added
	wizchip_check();
    
    // Allocate 2KB memory spaces to prevent Socket -3 (SOCKERR_BUFF) drops
    uint8_t tx_memsize[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rx_memsize[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    wizchip_init(tx_memsize, rx_memsize);

    wizchip_1ms_timer_initialize(repeating_timer_callback);	
	
    if (g_net_info.dhcp == NETINFO_DHCP) {
		//wizchip_dhcp_init();//htwe code has this function, this code try to make it easier
        DHCP_init(SOCKET_DHCP, g_ethernet_buf);
        reg_dhcp_cbfunc(wizchip_dhcp_assign, wizchip_dhcp_assign, wizchip_dhcp_conflict);
		
    } else {
        network_initialize(g_net_info);
        // Get network information 
        print_network_information(g_net_info);		
    }

    DNS_init(SOCKET_DNS, g_ethernet_buf);
}

void process_network_loop(void) {
	/*
    // Fail-safe protection boundary check
    if (!ethernet_hardware_alive) {
        return; 
    }
	*/
	
    // Software execution throttle window to avoid flooding the Wiznet hardware registers
    static uint32_t last_net_run = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_net_run < 10) { 
        return; 
    }
    last_net_run = now;
	
	tight_loop_contents();	//so far no issue yet

    // 1. Process DHCP Engine
    if (g_net_info.dhcp == NETINFO_DHCP) {
        net_retval = DHCP_run();

        if (net_retval == DHCP_IP_LEASED && g_dhcp_get_ip_flag == 0) {
            g_dhcp_get_ip_flag = 1;
            oled_dhcp_ip_flag  = 1;
        } 
    }

    // 2. Process TCP Server Engine 
    // Soft execution check: Only listen on port 5000 if we have acquired an IP space successfully
    if (g_dhcp_get_ip_flag == 1 || g_net_info.dhcp == NETINFO_STATIC) {
        int32_t tcp_state = cmd_tcps(SOCKET_CMD_TCP, g_cmd_tcp_buf, PORT_CMD_TCP, g_cmd_tcp_buf_out, g_cmd_tcp_flag);
        
        if (tcp_state < 0) {
            printf("[NET] Socket error occurred: %d. Resetting channel.\n", tcp_state);
            close(SOCKET_CMD_TCP); 
        } else {
            if (tcp_state > 1 && tcp_state < 255) {
                // Inline command routing extraction block
                char* cmd = (char*)g_cmd_tcp_buf;
                if (strncmp(cmd, "AT+FREQ=", 8) == 0) {
                    float target_hz;
                    if (sscanf(cmd, "AT+FREQ=%f", &target_hz) == 1 && target_hz >= 40.0f && target_hz <= 70.0f) {
                        update_system_frequency(target_hz);
                        send(SOCKET_CMD_TCP, (uint8_t*)"OK\r\n", 4);
                    }
                }
                // (Add your explicit AT+PWM or bypass calls here as required)
            } 
            else if (tcp_state == 254) { // Matches your project's completion token
                g_cmd_tcp_flag = 0;
            }
        }
    }
}
