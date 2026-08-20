
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_project_v01_test C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Set ethernet chip
set(WIZNET_CHIP W5100S)

if(${WIZNET_CHIP} STREQUAL W5100S)
    add_definitions(-D_WIZCHIP_=W5100S)
elseif(${WIZNET_CHIP} STREQUAL W5500)
    add_definitions(-D_WIZCHIP_=W5500)
else()
    message(FATAL_ERROR "WIZNET_CHIP is wrong = ${WIZNET_CHIP}")
endif()

message(STATUS "WIZNET_CHIP = ${WIZNET_CHIP}")

set(MyProjectName "pico_pwm_enet_v01")	#${MyProjectName}

set(FAMILY rp2040)
set(BOARD pico_sdk)

add_executable(${MyProjectName}
	#main.c
	)
	
target_sources(${MyProjectName} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src/main.c
        ${CMAKE_CURRENT_LIST_DIR}/src/wave_engine.c
		${CMAKE_CURRENT_LIST_DIR}/src/network_stack.c
		${CMAKE_CURRENT_LIST_DIR}/src/i2c_expanders.c
		${CMAKE_CURRENT_LIST_DIR}/src/ssd1306.c
		${CMAKE_CURRENT_LIST_DIR}/src/usb_console.c	
		${CMAKE_CURRENT_LIST_DIR}/src/at_commands.c			
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/w5x00_spi.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/w5x00_gpio_irq.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/timer.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Ethernet/socket.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Ethernet/wizchip_conf.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Ethernet/W5100S/w5100s.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Internet/DHCP/dhcp.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Internet/DNS/dns.c
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/cmd_tcp.c
		#${CMAKE_CURRENT_LIST_DIR}/device_drivers/hid_app.c
        #${CMAKE_CURRENT_LIST_DIR}/device_drivers/msc_app.c		
        )

# Make sure can find xx.h
target_include_directories(${MyProjectName} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Ethernet
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Ethernet/W5100S
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Internet/DHCP
		${CMAKE_CURRENT_LIST_DIR}/ioLibrary_Driver/Internet/DNS
		)	

target_link_libraries(${MyProjectName} 
	pico_stdlib 
	hardware_rtc 
	hardware_uart
	hardware_i2c
	hardware_spi
	hardware_dma
	hardware_pwm
    pico_multicore
    pico_util	
	#tinyusb_host 
	#tinyusb_board
	)

pico_enable_stdio_usb(${MyProjectName} 1)	#must set 0, if usb host or device are using
pico_enable_stdio_uart(${MyProjectName} 0)	#was 0

pico_add_extra_outputs(${MyProjectName})

#add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#    COMMAND arm-none-eabi-size ${CMAKE_CURRENT_LIST_DIR}/build/${PROJECT_NAME}.elf
#    VERBATIM
#)



