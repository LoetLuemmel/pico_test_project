/**
 * @file main.c
 * @brief CCS811 Air Quality Sensor - Iteration 3: Sensor Lifecycle Management
 *
 * Improvements in this iteration:
 * - Correct APP_START sequence with HW_ID (0x81) and HW_VERSION verification
 * - Read and display firmware versions (bootloader + application)
 * - Configurable measurement mode (default Mode 1 - 1 second)
 * - Flag readings as warming_up=true during first 20 minutes
 * - Track first_valid_ms only after warm-up period completes
 *
 * Hardware:
 * - Raspberry Pi Pico (RP2040)
 * - Joy-IT SEN-CCS811V1 sensor on I2C0 (GP4=SDA, GP5=SCL)
 * - Pico Debug Probe for SWD debugging and UART capture
 *
 * Output: UART0 at 115200 baud (captured by debug probe)
 */

#include <stdio.h>
#include <math.h>
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

// Measurement configuration (configurable)
#define SENSOR_MODE             CCS811_MODE_1SEC    // Default: 1 second interval
#define MEASURE_INTERVAL_MS     1000

// Statistics buffer size (60 samples for 60s at 1Hz)
#define STATS_BUFFER_SIZE       64

// Global sensor handle
static ccs811_t sensor;

// Metrics tracking
static uint32_t total_reads = 0;
static uint32_t total_fails = 0;
static uint32_t first_valid_ms = 0;          // First valid reading (any)
static uint32_t first_warmed_up_ms = 0;      // First reading after warm-up complete

// Running statistics (only includes post-warmup readings)
static uint16_t eco2_buffer[STATS_BUFFER_SIZE];
static uint16_t tvoc_buffer[STATS_BUFFER_SIZE];
static uint32_t buffer_index = 0;
static uint32_t buffer_count = 0;

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
static void toggle_led(void) {
    static bool led_state = false;
    led_state = !led_state;
    gpio_put(LED_PIN, led_state);
}

/**
 * @brief Add sample to statistics buffer (only for warmed-up readings)
 */
static void add_sample(uint16_t eco2, uint16_t tvoc) {
    eco2_buffer[buffer_index] = eco2;
    tvoc_buffer[buffer_index] = tvoc;
    buffer_index = (buffer_index + 1) % STATS_BUFFER_SIZE;
    if (buffer_count < STATS_BUFFER_SIZE) {
        buffer_count++;
    }
}

/**
 * @brief Compute mean of buffer
 */
static float compute_mean(const uint16_t *buffer, uint32_t count) {
    if (count == 0) return 0.0f;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        sum += buffer[i];
    }
    return (float)sum / (float)count;
}

/**
 * @brief Compute standard deviation of buffer
 */
static float compute_stddev(const uint16_t *buffer, uint32_t count, float mean) {
    if (count < 2) return 0.0f;
    float sum_sq = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        float diff = (float)buffer[i] - mean;
        sum_sq += diff * diff;
    }
    return sqrtf(sum_sq / (float)(count - 1));
}

/**
 * @brief Get mode name string
 */
static const char* get_mode_name(ccs811_mode_t mode) {
    switch (mode) {
        case CCS811_MODE_IDLE:   return "Idle";
        case CCS811_MODE_1SEC:   return "1sec";
        case CCS811_MODE_10SEC:  return "10sec";
        case CCS811_MODE_60SEC:  return "60sec";
        case CCS811_MODE_250MS:  return "250ms";
        default:                 return "Unknown";
    }
}

/**
 * @brief Print summary statistics
 */
static void print_summary(uint32_t uptime_s) {
    float eco2_mean = compute_mean(eco2_buffer, buffer_count);
    float eco2_stddev = compute_stddev(eco2_buffer, buffer_count, eco2_mean);
    float tvoc_mean = compute_mean(tvoc_buffer, buffer_count);
    float tvoc_stddev = compute_stddev(tvoc_buffer, buffer_count, tvoc_mean);

    uint32_t total = total_reads + total_fails;
    float fail_rate = (total > 0) ? ((float)total_fails / (float)total * 100.0f) : 0.0f;

    bool warmed_up = ccs811_is_warmed_up(&sensor);

    printf("[SUMMARY] reads=%lu fails=%lu fail_rate=%.2f eco2_mean=%.1f eco2_stddev=%.1f tvoc_mean=%.1f tvoc_stddev=%.1f uptime_s=%lu warmed_up=%s\n",
           total_reads, total_fails, fail_rate,
           eco2_mean, eco2_stddev, tvoc_mean, tvoc_stddev, uptime_s,
           warmed_up ? "true" : "false");
}

int main() {
    // Initialize stdio for UART output (captured by debug probe)
    stdio_init_all();

    // Initialize hardware
    init_led();
    init_i2c();

    // Brief delay for UART to stabilize
    sleep_ms(100);

    printf("\n");
    printf("[INFO] CCS811 Iteration 3: Sensor Lifecycle Management\n");
    printf("[INFO] I2C: GP%d (SDA), GP%d (SCL), %d Hz\n", I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

    // Initialize CCS811 sensor with configurable mode
    printf("[INFO] Initializing CCS811 at 0x%02X with mode %s...\n",
           CCS811_I2C_ADDR_DEFAULT, get_mode_name(SENSOR_MODE));

    ccs811_error_t err = ccs811_init_with_mode(&sensor, I2C_PORT, CCS811_I2C_ADDR_DEFAULT, SENSOR_MODE);

    if (err != CCS811_OK) {
        printf("[ERROR] Init failed: %s\n", ccs811_error_string(err));
        // Error indication: rapid LED blink
        while (true) {
            toggle_led();
            sleep_ms(100);
        }
    }

    // Print sensor identification info
    printf("[INFO] CCS811 initialized successfully!\n");
    printf("[INFO] HW_ID=0x%02X HW_VER=0x%02X\n",
           ccs811_get_hw_id(&sensor),
           ccs811_get_hw_version(&sensor));
    printf("[INFO] FW_BOOT=%d.%d.%d FW_APP=%d.%d.%d\n",
           (ccs811_get_fw_boot_version(&sensor) >> 12) & 0xF,
           (ccs811_get_fw_boot_version(&sensor) >> 8) & 0xF,
           ccs811_get_fw_boot_version(&sensor) & 0xFF,
           (ccs811_get_fw_app_version(&sensor) >> 12) & 0xF,
           (ccs811_get_fw_app_version(&sensor) >> 8) & 0xF,
           ccs811_get_fw_app_version(&sensor) & 0xFF);
    printf("[INFO] Mode=%s, Warm-up=20min required\n", get_mode_name(ccs811_get_mode(&sensor)));
    printf("[INFO] Starting measurement loop...\n");

    uint32_t boot_time_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_summary_s = 0;

    // Main measurement loop
    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t uptime_ms = now_ms - boot_time_ms;
        uint32_t uptime_s = uptime_ms / 1000;

        // Check warm-up status
        bool warming_up = !ccs811_is_warmed_up(&sensor);

        // Try to read data using robust function
        ccs811_data_t data = {0};
        err = ccs811_read_data_robust(&sensor, &data);

        if (err == CCS811_OK) {
            // Successful read
            total_reads++;

            // Track first valid reading (any reading)
            if (first_valid_ms == 0) {
                first_valid_ms = uptime_ms;
            }

            // Track first reading after warm-up and add to stats
            if (!warming_up) {
                if (first_warmed_up_ms == 0) {
                    first_warmed_up_ms = uptime_ms;
                }
                // Only add post-warmup readings to statistics buffer
                add_sample(data.eco2, data.tvoc);
            }

            // Output metric with warming_up flag
            printf("[METRIC] read_ok=1 eco2=%u tvoc=%u ts_ms=%lu warming_up=%s\n",
                   data.eco2, data.tvoc, uptime_ms,
                   warming_up ? "true" : "false");

            toggle_led();  // Toggle LED on successful read

        } else if (err == CCS811_ERR_NOT_READY) {
            // Data not ready - not counted as failure, just skip
            // (normal if polling faster than sensor rate)

        } else {
            // Actual failure
            total_fails++;
            printf("[METRIC] read_fail=1 err=%s ts_ms=%lu warming_up=%s\n",
                   ccs811_error_string(err), uptime_ms,
                   warming_up ? "true" : "false");
        }

        // Print summary every 10 seconds
        if (uptime_s >= last_summary_s + 10 && uptime_s > 0) {
            print_summary(uptime_s);
            last_summary_s = uptime_s;
        }

        // Wait for next measurement interval
        sleep_ms(MEASURE_INTERVAL_MS);
    }

    return 0;
}
