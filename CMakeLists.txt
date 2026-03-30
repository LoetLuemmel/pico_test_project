cmake_minimum_required(VERSION 3.13)

# Initialize the Pico SDK
include(pico_sdk_import.cmake)

project(pico_test_project C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable
add_executable(pico_test
    main.c
)

# Pull in common dependencies
target_link_libraries(pico_test
    pico_stdlib
    hardware_gpio
    hardware_adc
)

# Enable USB output, disable UART output (can be changed)
pico_enable_stdio_usb(pico_test 1)
pico_enable_stdio_uart(pico_test 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_test)
