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
    src/drivers/ccs811.c
)

# Include directories
target_include_directories(pico_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers
)

# Pull in common dependencies
target_link_libraries(pico_test
    pico_stdlib
    hardware_gpio
    hardware_adc
    hardware_i2c
    hardware_flash
    hardware_sync
)

# Enable UART output (debug probe), disable USB output
pico_enable_stdio_usb(pico_test 0)
pico_enable_stdio_uart(pico_test 1)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_test)
