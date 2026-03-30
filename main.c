/**
 * Pico Test Project - Basic I/O Testing with Debug Probe Support
 *
 * This program tests basic GPIO functionality and LED control on the
 * Raspberry Pi Pico. It's designed to work with the Pico Debug Probe
 * for step-through debugging and breakpoint testing.
 *
 * Hardware:
 * - Raspberry Pi Pico (RP2040)
 * - Pico Debug Probe connected via SWD
 *
 * Onboard LED: GPIO 25 (standard Pico)
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// Onboard LED pin (GPIO 25 for standard Pico)
#define LED_PIN 25

// Test GPIO pins - choose unused pins for your setup
#define TEST_OUTPUT_PIN 16
#define TEST_INPUT_PIN 17

// Test counter for debugging
volatile uint32_t loop_counter = 0;
volatile bool led_state = false;

/**
 * Initialize all GPIO pins for testing
 */
void init_gpio(void) {
    // Initialize onboard LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    // Initialize test output pin
    gpio_init(TEST_OUTPUT_PIN);
    gpio_set_dir(TEST_OUTPUT_PIN, GPIO_OUT);
    gpio_put(TEST_OUTPUT_PIN, 0);

    // Initialize test input pin with pull-down
    gpio_init(TEST_INPUT_PIN);
    gpio_set_dir(TEST_INPUT_PIN, GPIO_IN);
    gpio_pull_down(TEST_INPUT_PIN);
}

/**
 * Toggle the onboard LED
 * Good breakpoint location for testing debug probe
 */
__attribute__((noinline)) void toggle_led(void) {
    led_state = !led_state;
    gpio_put(LED_PIN, led_state);
}

/**
 * Test GPIO output functionality
 * Toggles test output pin and reads it back if looped to input
 */
bool test_gpio_output(void) {
    // Set output high
    gpio_put(TEST_OUTPUT_PIN, 1);
    sleep_ms(10);

    // If GPIO16 is connected to GPIO17, we can verify
    bool read_high = gpio_get(TEST_INPUT_PIN);

    // Set output low
    gpio_put(TEST_OUTPUT_PIN, 0);
    sleep_ms(10);

    bool read_low = gpio_get(TEST_INPUT_PIN);

    // If pins are connected: high should read 1, low should read 0
    // If not connected: both will read based on pull-down (0)
    return true; // Basic test always passes, check values in debugger
}

/**
 * Print system status - useful for USB serial debugging
 */
void print_status(void) {
    printf("\n=== Pico Test Status ===\n");
    printf("Loop count: %lu\n", loop_counter);
    printf("LED state: %s\n", led_state ? "ON" : "OFF");
    printf("GPIO %d (input): %d\n", TEST_INPUT_PIN, gpio_get(TEST_INPUT_PIN));
    printf("========================\n");
}

int main() {
    // Initialize stdio for USB serial output
    stdio_init_all();

    // Wait for USB connection (optional, helps with serial monitor)
    sleep_ms(2000);

    printf("\n\n");
    printf("================================\n");
    printf("  Pico Test Project Started\n");
    printf("  Debug Probe Ready\n");
    printf("================================\n");
    printf("\nInitializing GPIO...\n");

    // Initialize GPIO pins
    init_gpio();

    printf("GPIO initialized successfully.\n");
    printf("LED Pin: GPIO %d\n", LED_PIN);
    printf("Test Output: GPIO %d\n", TEST_OUTPUT_PIN);
    printf("Test Input: GPIO %d\n", TEST_INPUT_PIN);
    printf("\nStarting main loop...\n");
    printf("(Set breakpoints in toggle_led() for debug testing)\n\n");

    // Main loop
    while (true) {
        // Increment counter (watch this variable in debugger)
        loop_counter++;

        // Toggle LED - good place for breakpoint
        toggle_led();  // <-- Set breakpoint here

        // Run GPIO test
        test_gpio_output();

        // Print status every 10 iterations
        if (loop_counter % 10 == 0) {
            print_status();
        }

        // Wait 500ms between toggles (LED blinks at 1Hz)
        sleep_ms(500);
    }

    return 0;
}
