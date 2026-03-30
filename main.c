/**
 * @file main.c
 * @brief CCS811 Air Quality Sensor Test Application
 *
 * Reads eCO2 and TVOC data from the Joy-IT CCS811 sensor via I2C
 * and outputs measurements via USB serial.
 *
 * Hardware:
 * - Raspberry Pi Pico (RP2040)
 * - Joy-IT SEN-CCS811V1 sensor on I2C0 (GP4=SDA, GP5=SCL)
 * - Pico Debug Probe for SWD debugging
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "ccs811.h"

// I2C Configuration
#define I2C_PORT        i2c0
#define I2C_SDA_PIN     4
#define I2C_SCL_PIN     5
#define I2C_FREQ_HZ     100000  // 100 kHz standard mode

// Onboard LED pin
#define LED_PIN         25

// Measurement interval in milliseconds
#define MEASURE_INTERVAL_MS     1000

// Global sensor handle
static ccs811_t sensor;

// Measurement counter
static uint32_t measurement_count = 0;

/**
 * @brief Initialize I2C bus
 */
static void init_i2c(void) {
    i2c_init(I2C_PORT, I2C_FREQ_HZ);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

/**
 * @brief Initialize onboard LED
 */
static void init_led(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
}

/**
 * @brief Toggle LED state
 */
__attribute__((noinline)) static void toggle_led(void) {
    static bool led_state = false;
    led_state = !led_state;
    gpio_put(LED_PIN, led_state);
}

/**
 * @brief Print CSV header
 */
static void print_header(void) {
    printf("\n# CCS811 Sensor Data Log\n");
    printf("# Format: count,eco2_ppm,tvoc_ppb,status,warmed_up,warmup_remaining_s\n");
    printf("count,eco2_ppm,tvoc_ppb,status,warmed_up,warmup_remaining_s\n");
}

/**
 * @brief Print measurement data in CSV format
 */
static void print_measurement(const ccs811_data_t *data) {
    bool warmed_up = ccs811_is_warmed_up(&sensor);
    uint32_t warmup_remaining = ccs811_warmup_seconds_remaining(&sensor);

    printf("%lu,%u,%u,%s,%s,%lu\n",
           measurement_count,
           data->eco2,
           data->tvoc,
           data->error ? "ERROR" : "OK",
           warmed_up ? "true" : "false",
           warmup_remaining);
}

/**
 * @brief Print sensor error details
 */
static void print_error(uint8_t error_id) {
    printf("[ERROR] Sensor error flags: 0x%02X\n", error_id);

    if (error_id & CCS811_ERROR_WRITE_REG)
        printf("  - Invalid register write\n");
    if (error_id & CCS811_ERROR_READ_REG)
        printf("  - Invalid register read\n");
    if (error_id & CCS811_ERROR_MEASMODE)
        printf("  - Invalid measurement mode\n");
    if (error_id & CCS811_ERROR_MAX_RESIST)
        printf("  - Sensor resistance too high\n");
    if (error_id & CCS811_ERROR_HEATER_FAULT)
        printf("  - Heater fault\n");
    if (error_id & CCS811_ERROR_HEATER_SUPPLY)
        printf("  - Heater supply fault\n");
}

int main() {
    // Initialize stdio for USB serial output
    stdio_init_all();

    // Initialize hardware
    init_led();
    init_i2c();

    // Wait for USB connection
    sleep_ms(2000);

    printf("\n");
    printf("========================================\n");
    printf("  CCS811 Air Quality Sensor Test\n");
    printf("  Pico Test Project - embedded-device\n");
    printf("========================================\n\n");

    printf("[INFO] Initializing I2C on GP%d (SDA) and GP%d (SCL)...\n",
           I2C_SDA_PIN, I2C_SCL_PIN);
    printf("[INFO] I2C frequency: %d Hz\n", I2C_FREQ_HZ);

    // Initialize CCS811 sensor
    printf("[INFO] Initializing CCS811 sensor at address 0x%02X...\n",
           CCS811_I2C_ADDR_DEFAULT);

    ccs811_error_t err = ccs811_init(&sensor, I2C_PORT, CCS811_I2C_ADDR_DEFAULT);

    if (err != CCS811_OK) {
        printf("[ERROR] Failed to initialize CCS811: %s\n", ccs811_error_string(err));
        printf("[ERROR] Check wiring and sensor connection!\n");
        printf("\n");
        printf("Expected wiring:\n");
        printf("  CCS811 VIN  -> Pico 3V3 (Pin 36)\n");
        printf("  CCS811 GND  -> Pico GND (Pin 38)\n");
        printf("  CCS811 SDA  -> Pico GP4 (Pin 6)\n");
        printf("  CCS811 SCL  -> Pico GP5 (Pin 7)\n");
        printf("  CCS811 WAKE -> Pico GND (Pin 38)\n");
        printf("\n");

        // Error indication: rapid LED blink
        while (true) {
            toggle_led();
            sleep_ms(100);
        }
    }

    // Sensor initialized successfully
    uint8_t hw_id = ccs811_get_hw_id(&sensor);
    printf("[INFO] CCS811 initialized successfully!\n");
    printf("[INFO] Hardware ID: 0x%02X\n", hw_id);
    printf("[WARN] Sensor needs 20 minutes to warm up for accurate readings.\n");
    printf("\n");

    // Print CSV header
    print_header();

    // Main measurement loop
    while (true) {
        // Check if data is ready
        if (ccs811_data_ready(&sensor)) {
            ccs811_data_t data;
            err = ccs811_read_data(&sensor, &data);

            if (err == CCS811_OK) {
                measurement_count++;
                print_measurement(&data);
                toggle_led();  // Toggle LED on successful read
            } else if (err == CCS811_ERR_SENSOR) {
                printf("[ERROR] Sensor reported error\n");
                print_error(data.error_id);
            } else {
                printf("[ERROR] Failed to read data: %s\n", ccs811_error_string(err));
            }
        }

        // Wait for next measurement interval
        sleep_ms(MEASURE_INTERVAL_MS);
    }

    return 0;
}
