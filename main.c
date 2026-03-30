/**
 * @file main.c
 * @brief CCS811 Air Quality Sensor - Iteration 4: Signal Conditioning
 *
 * Improvements in this iteration:
 * - Moving-average filter (configurable window, default N=5)
 * - Outlier rejection (discard readings > 3σ from running mean)
 * - Breath test detection with step-response metrics
 * - [BREATH_TEST] output with peak_ppm, rise_time_ms, settle_time_ms
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

// Measurement configuration
#define SENSOR_MODE             CCS811_MODE_1SEC
#define MEASURE_INTERVAL_MS     1000

// Signal conditioning parameters (iteration 4)
#define FILTER_WINDOW_SIZE      5       // Moving average window (N=5)
#define OUTLIER_SIGMA_THRESHOLD 3.0f    // Reject readings > 3σ from mean
#define BASELINE_ECO2           400     // Baseline eCO2 in clean air

// Breath test detection thresholds
#define BREATH_DETECT_THRESHOLD 50      // eCO2 rise above baseline to detect breath
#define BREATH_SETTLE_THRESHOLD 20      // eCO2 must return within this of baseline
#define BREATH_MIN_PEAK         500     // Minimum peak eCO2 for valid breath test

// Statistics buffer size
#define STATS_BUFFER_SIZE       64

// Global sensor handle
static ccs811_t sensor;

// Metrics tracking
static uint32_t total_reads = 0;
static uint32_t total_fails = 0;
static uint32_t total_outliers = 0;
static uint32_t first_valid_ms = 0;
static uint32_t first_warmed_up_ms = 0;

// Moving average filter state
static uint16_t ma_eco2_buffer[FILTER_WINDOW_SIZE];
static uint16_t ma_tvoc_buffer[FILTER_WINDOW_SIZE];
static uint32_t ma_index = 0;
static uint32_t ma_count = 0;

// Running statistics for outlier detection
static float running_eco2_mean = 0.0f;
static float running_eco2_var = 0.0f;
static uint32_t running_count = 0;

// Statistics buffer (filtered values only)
static uint16_t eco2_buffer[STATS_BUFFER_SIZE];
static uint16_t tvoc_buffer[STATS_BUFFER_SIZE];
static uint32_t buffer_index = 0;
static uint32_t buffer_count = 0;

// Breath test state machine
typedef enum {
    BREATH_STATE_IDLE,      // Waiting for breath event
    BREATH_STATE_PROMPTING, // LED on, waiting for user to breathe
    BREATH_STATE_RISING,    // Detected rise, tracking to peak
    BREATH_STATE_FALLING,   // Past peak, waiting for settle
    BREATH_STATE_DONE       // Test complete, report pending
} breath_state_t;

static breath_state_t breath_state = BREATH_STATE_IDLE;
static uint16_t breath_baseline = BASELINE_ECO2;
static uint16_t breath_peak = 0;
static uint32_t breath_start_ms = 0;
static uint32_t breath_peak_ms = 0;
static uint32_t breath_settle_ms = 0;
static uint32_t breath_prompt_start_ms = 0;

// Breath test configuration
#define BREATH_PROMPT_TIME_MS   15000   // Turn LED on at 15 seconds
#define BREATH_PROMPT_TIMEOUT_MS 20000  // LED timeout if no breath detected

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
 * @brief Update running mean and variance (Welford's algorithm)
 */
static void update_running_stats(uint16_t eco2) {
    running_count++;
    float delta = (float)eco2 - running_eco2_mean;
    running_eco2_mean += delta / (float)running_count;
    float delta2 = (float)eco2 - running_eco2_mean;
    running_eco2_var += delta * delta2;
}

/**
 * @brief Get running standard deviation
 */
static float get_running_stddev(void) {
    if (running_count < 2) return 100.0f;  // Large default to allow initial readings
    return sqrtf(running_eco2_var / (float)(running_count - 1));
}

/**
 * @brief Check if reading is an outlier (> 3σ from running mean)
 */
static bool is_outlier(uint16_t eco2) {
    if (running_count < FILTER_WINDOW_SIZE) return false;  // Not enough data yet

    float stddev = get_running_stddev();
    float diff = fabsf((float)eco2 - running_eco2_mean);
    return diff > (OUTLIER_SIGMA_THRESHOLD * stddev);
}

/**
 * @brief Add sample to moving average filter
 */
static void ma_add_sample(uint16_t eco2, uint16_t tvoc) {
    ma_eco2_buffer[ma_index] = eco2;
    ma_tvoc_buffer[ma_index] = tvoc;
    ma_index = (ma_index + 1) % FILTER_WINDOW_SIZE;
    if (ma_count < FILTER_WINDOW_SIZE) {
        ma_count++;
    }
}

/**
 * @brief Get moving average of eCO2
 */
static uint16_t ma_get_eco2(void) {
    if (ma_count == 0) return 0;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ma_count; i++) {
        sum += ma_eco2_buffer[i];
    }
    return (uint16_t)(sum / ma_count);
}

/**
 * @brief Get moving average of TVOC
 */
static uint16_t ma_get_tvoc(void) {
    if (ma_count == 0) return 0;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ma_count; i++) {
        sum += ma_tvoc_buffer[i];
    }
    return (uint16_t)(sum / ma_count);
}

/**
 * @brief Add sample to statistics buffer
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
 * @brief Update breath test state machine
 */
static void update_breath_test(uint16_t eco2_filtered, uint32_t ts_ms) {
    switch (breath_state) {
        case BREATH_STATE_IDLE:
            // Update baseline from stable readings
            if (eco2_filtered < breath_baseline + 30) {
                breath_baseline = (breath_baseline * 7 + eco2_filtered) / 8;  // Slow tracking
            }
            // Check if it's time to prompt for breath test
            if (ts_ms >= BREATH_PROMPT_TIME_MS && breath_prompt_start_ms == 0) {
                breath_state = BREATH_STATE_PROMPTING;
                breath_prompt_start_ms = ts_ms;
                gpio_put(LED_PIN, 1);  // LED ON - user should breathe now
                printf("[BREATH_PROMPT] LED ON - breathe on sensor now!\n");
            }
            break;

        case BREATH_STATE_PROMPTING:
            // LED is on, waiting for user to breathe
            // Detect breath event (significant rise above baseline)
            if (eco2_filtered > breath_baseline + BREATH_DETECT_THRESHOLD) {
                breath_state = BREATH_STATE_RISING;
                breath_start_ms = ts_ms;
                breath_peak = eco2_filtered;
                breath_peak_ms = ts_ms;
                gpio_put(LED_PIN, 0);  // LED OFF - breath detected
                printf("[BREATH_DETECT] Breath detected, LED OFF\n");
            } else if (ts_ms > breath_prompt_start_ms + BREATH_PROMPT_TIMEOUT_MS) {
                // Timeout - no breath detected
                breath_state = BREATH_STATE_IDLE;
                breath_prompt_start_ms = 0;
                gpio_put(LED_PIN, 0);  // LED OFF
                printf("[BREATH_TIMEOUT] No breath detected, LED OFF\n");
            }
            break;

        case BREATH_STATE_RISING:
            // Track rising edge to peak
            if (eco2_filtered > breath_peak) {
                breath_peak = eco2_filtered;
                breath_peak_ms = ts_ms;
            } else if (eco2_filtered < breath_peak - 20) {
                // Started falling
                breath_state = BREATH_STATE_FALLING;
            }
            break;

        case BREATH_STATE_FALLING:
            // Wait for settle (return to near baseline)
            if (eco2_filtered <= breath_baseline + BREATH_SETTLE_THRESHOLD) {
                breath_settle_ms = ts_ms;
                breath_state = BREATH_STATE_DONE;
            }
            break;

        case BREATH_STATE_DONE:
            // Report handled in main loop
            break;
    }
}

/**
 * @brief Report breath test results
 */
static void report_breath_test(void) {
    if (breath_state != BREATH_STATE_DONE) return;
    if (breath_peak < BREATH_MIN_PEAK) {
        // Not a valid breath test (peak too low)
        breath_state = BREATH_STATE_IDLE;
        breath_prompt_start_ms = 0;
        return;
    }

    uint32_t rise_time_ms = breath_peak_ms - breath_start_ms;
    uint32_t settle_time_ms = breath_settle_ms - breath_peak_ms;
    uint32_t total_time_ms = breath_settle_ms - breath_start_ms;

    printf("[BREATH_TEST] peak_ppm=%u baseline_ppm=%u rise_time_ms=%lu settle_time_ms=%lu total_ms=%lu\n",
           breath_peak, breath_baseline, rise_time_ms, settle_time_ms, total_time_ms);

    // Reset for next test
    breath_state = BREATH_STATE_IDLE;
    breath_peak = 0;
    breath_prompt_start_ms = 0;
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

    printf("[SUMMARY] reads=%lu fails=%lu outliers=%lu fail_rate=%.2f eco2_mean=%.1f eco2_stddev=%.1f tvoc_mean=%.1f tvoc_stddev=%.1f uptime_s=%lu warmed_up=%s\n",
           total_reads, total_fails, total_outliers, fail_rate,
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
    printf("[INFO] CCS811 Iteration 4: Signal Conditioning\n");
    printf("[INFO] I2C: GP%d (SDA), GP%d (SCL), %d Hz\n", I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
    printf("[INFO] Filter: MA window=%d, outlier threshold=%.1f sigma\n",
           FILTER_WINDOW_SIZE, OUTLIER_SIGMA_THRESHOLD);

    // Initialize CCS811 sensor
    printf("[INFO] Initializing CCS811 at 0x%02X with mode %s...\n",
           CCS811_I2C_ADDR_DEFAULT, get_mode_name(SENSOR_MODE));

    ccs811_error_t err = ccs811_init_with_mode(&sensor, I2C_PORT, CCS811_I2C_ADDR_DEFAULT, SENSOR_MODE);

    if (err != CCS811_OK) {
        printf("[ERROR] Init failed: %s\n", ccs811_error_string(err));
        while (true) {
            toggle_led();
            sleep_ms(100);
        }
    }

    printf("[INFO] CCS811 initialized! HW_ID=0x%02X HW_VER=0x%02X\n",
           ccs811_get_hw_id(&sensor),
           ccs811_get_hw_version(&sensor));
    printf("[INFO] Starting measurement loop with signal conditioning...\n");

    uint32_t boot_time_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_summary_s = 0;

    // Main measurement loop
    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        uint32_t uptime_ms = now_ms - boot_time_ms;
        uint32_t uptime_s = uptime_ms / 1000;

        bool warming_up = !ccs811_is_warmed_up(&sensor);

        // Read raw data
        ccs811_data_t data = {0};
        err = ccs811_read_data_robust(&sensor, &data);

        if (err == CCS811_OK) {
            total_reads++;

            if (first_valid_ms == 0) {
                first_valid_ms = uptime_ms;
            }

            // Check for outlier
            bool outlier = is_outlier(data.eco2);
            if (outlier) {
                total_outliers++;
                printf("[METRIC] read_ok=1 eco2=%u tvoc=%u ts_ms=%lu warming_up=%s outlier=true\n",
                       data.eco2, data.tvoc, uptime_ms,
                       warming_up ? "true" : "false");
            } else {
                // Update running stats with valid reading
                update_running_stats(data.eco2);

                // Add to moving average filter
                ma_add_sample(data.eco2, data.tvoc);

                // Get filtered values
                uint16_t eco2_filtered = ma_get_eco2();
                uint16_t tvoc_filtered = ma_get_tvoc();

                // Update breath test state machine
                update_breath_test(eco2_filtered, uptime_ms);

                // Output raw and filtered values
                printf("[METRIC] read_ok=1 eco2=%u tvoc=%u eco2_filt=%u tvoc_filt=%u ts_ms=%lu warming_up=%s outlier=false\n",
                       data.eco2, data.tvoc, eco2_filtered, tvoc_filtered, uptime_ms,
                       warming_up ? "true" : "false");

                // Add filtered values to stats buffer (post-warmup only)
                if (!warming_up) {
                    if (first_warmed_up_ms == 0) {
                        first_warmed_up_ms = uptime_ms;
                    }
                    add_sample(eco2_filtered, tvoc_filtered);
                }
            }

            // Report breath test if complete
            report_breath_test();

        } else if (err == CCS811_ERR_NOT_READY) {
            // Skip
        } else {
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

        sleep_ms(MEASURE_INTERVAL_MS);
    }

    return 0;
}
