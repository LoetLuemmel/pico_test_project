/**
 * @file ccs811.h
 * @brief CCS811 Air Quality Sensor Driver for Raspberry Pi Pico
 *
 * Driver for the Joy-IT SEN-CCS811V1 air quality sensor.
 * Measures eCO2 (400-8192 ppm) and TVOC (0-1187 ppb) via I2C.
 */

#ifndef CCS811_H
#define CCS811_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

// I2C Address (ADDR pin low = 0x5A, high = 0x5B)
#define CCS811_I2C_ADDR_DEFAULT     0x5A
#define CCS811_I2C_ADDR_ALT         0x5B

// Hardware ID (must be 0x81 for CCS811)
#define CCS811_HW_ID_VALUE          0x81

// Register addresses
#define CCS811_REG_STATUS           0x00
#define CCS811_REG_MEAS_MODE        0x01
#define CCS811_REG_ALG_RESULT_DATA  0x02
#define CCS811_REG_RAW_DATA         0x03
#define CCS811_REG_ENV_DATA         0x05
#define CCS811_REG_THRESHOLDS       0x10
#define CCS811_REG_BASELINE         0x11
#define CCS811_REG_HW_ID            0x20
#define CCS811_REG_HW_VERSION       0x21
#define CCS811_REG_FW_BOOT_VERSION  0x23
#define CCS811_REG_FW_APP_VERSION   0x24
#define CCS811_REG_ERROR_ID         0xE0
#define CCS811_REG_APP_START        0xF4
#define CCS811_REG_SW_RESET         0xFF

// STATUS register bits
#define CCS811_STATUS_FW_MODE       (1 << 7)
#define CCS811_STATUS_APP_VALID     (1 << 4)
#define CCS811_STATUS_DATA_READY    (1 << 3)
#define CCS811_STATUS_ERROR         (1 << 0)

// ERROR_ID register bits
#define CCS811_ERROR_WRITE_REG      (1 << 0)
#define CCS811_ERROR_READ_REG       (1 << 1)
#define CCS811_ERROR_MEASMODE       (1 << 2)
#define CCS811_ERROR_MAX_RESIST     (1 << 3)
#define CCS811_ERROR_HEATER_FAULT   (1 << 4)
#define CCS811_ERROR_HEATER_SUPPLY  (1 << 5)

// Measurement modes (DRIVE_MODE in MEAS_MODE register)
typedef enum {
    CCS811_MODE_IDLE    = 0,    // Idle, low current mode
    CCS811_MODE_1SEC    = 1,    // Constant power, measurement every 1 second
    CCS811_MODE_10SEC   = 2,    // Pulse heating, measurement every 10 seconds
    CCS811_MODE_60SEC   = 3,    // Low power pulse, measurement every 60 seconds
    CCS811_MODE_250MS   = 4     // Constant power, measurement every 250ms (RAW only)
} ccs811_mode_t;

// Error codes
typedef enum {
    CCS811_OK               = 0,
    CCS811_ERR_I2C          = -1,
    CCS811_ERR_HW_ID        = -2,
    CCS811_ERR_APP_INVALID  = -3,
    CCS811_ERR_APP_START    = -4,
    CCS811_ERR_SENSOR       = -5,
    CCS811_ERR_I2C_TIMEOUT  = -6,
    CCS811_ERR_NOT_READY    = -7,
    CCS811_ERR_HW_ERROR     = -8
} ccs811_error_t;

// Sensor data structure
typedef struct {
    uint16_t eco2;          // eCO2 in ppm (400-8192)
    uint16_t tvoc;          // TVOC in ppb (0-1187)
    uint8_t  status;        // Raw status byte
    uint8_t  error_id;      // Error ID if error occurred
    bool     data_ready;    // New data available
    bool     error;         // Error flag
} ccs811_data_t;

// I2C retry configuration
#define CCS811_I2C_MAX_RETRIES      3
#define CCS811_I2C_RETRY_DELAY_1_MS 10
#define CCS811_I2C_RETRY_DELAY_2_MS 50
#define CCS811_I2C_RETRY_DELAY_3_MS 200

// Warm-up time in milliseconds (20 minutes)
#define CCS811_WARMUP_TIME_MS   (20 * 60 * 1000)

// Device handle
typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    bool initialized;
    uint32_t init_time_ms;  // Time when sensor was initialized (for warm-up tracking)
    // I2C statistics (iteration 2)
    uint32_t i2c_retries;       // Total retry attempts
    uint32_t i2c_failures;      // Total I2C failures after all retries
    uint8_t last_error_id;      // Last ERROR_ID register value
    uint8_t last_status;        // Last STATUS register value
    // Sensor info (iteration 3)
    uint8_t hw_id;              // Hardware ID (should be 0x81)
    uint8_t hw_version;         // Hardware version
    uint16_t fw_boot_version;   // Firmware bootloader version
    uint16_t fw_app_version;    // Firmware application version
    ccs811_mode_t mode;         // Current measurement mode
} ccs811_t;

/**
 * @brief Initialize the CCS811 sensor with default mode (Mode 1 - 1 second)
 *
 * @param dev Pointer to device handle
 * @param i2c I2C instance (i2c0 or i2c1)
 * @param addr I2C address (0x5A or 0x5B)
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_init(ccs811_t *dev, i2c_inst_t *i2c, uint8_t addr);

/**
 * @brief Initialize the CCS811 sensor with configurable measurement mode
 *
 * Performs the complete initialization sequence:
 * 1. Wait 20ms for sensor power-on
 * 2. Verify HW_ID reads 0x81
 * 3. Read and store HW_VERSION, FW_BOOT_VERSION, FW_APP_VERSION
 * 4. Check STATUS register for APP_VALID bit
 * 5. Write APP_START command to register 0xF4
 * 6. Verify STATUS FW_MODE bit is set (now in application mode)
 * 7. Set the requested measurement mode
 *
 * @param dev Pointer to device handle
 * @param i2c I2C instance (i2c0 or i2c1)
 * @param addr I2C address (0x5A or 0x5B)
 * @param mode Measurement mode (CCS811_MODE_*)
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_init_with_mode(ccs811_t *dev, i2c_inst_t *i2c, uint8_t addr, ccs811_mode_t mode);

/**
 * @brief Set the measurement mode
 *
 * @param dev Pointer to device handle
 * @param mode Measurement mode (CCS811_MODE_*)
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_set_mode(ccs811_t *dev, ccs811_mode_t mode);

/**
 * @brief Check if new data is available
 *
 * @param dev Pointer to device handle
 * @return true if data ready, false otherwise
 */
bool ccs811_data_ready(ccs811_t *dev);

/**
 * @brief Read eCO2 and TVOC data from sensor
 *
 * @param dev Pointer to device handle
 * @param data Pointer to data structure to fill
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_read_data(ccs811_t *dev, ccs811_data_t *data);

/**
 * @brief Get sensor status register
 *
 * @param dev Pointer to device handle
 * @return Status byte
 */
uint8_t ccs811_get_status(ccs811_t *dev);

/**
 * @brief Get error ID register
 *
 * @param dev Pointer to device handle
 * @return Error ID byte
 */
uint8_t ccs811_get_error_id(ccs811_t *dev);

/**
 * @brief Read hardware ID
 *
 * @param dev Pointer to device handle
 * @return Hardware ID (should be 0x81)
 */
uint8_t ccs811_get_hw_id(ccs811_t *dev);

/**
 * @brief Get hardware version (cached from init)
 *
 * @param dev Pointer to device handle
 * @return Hardware version byte
 */
uint8_t ccs811_get_hw_version(ccs811_t *dev);

/**
 * @brief Get firmware bootloader version (cached from init)
 *
 * @param dev Pointer to device handle
 * @return Bootloader version (major.minor packed)
 */
uint16_t ccs811_get_fw_boot_version(ccs811_t *dev);

/**
 * @brief Get firmware application version (cached from init)
 *
 * @param dev Pointer to device handle
 * @return Application version (major.minor packed)
 */
uint16_t ccs811_get_fw_app_version(ccs811_t *dev);

/**
 * @brief Get current measurement mode
 *
 * @param dev Pointer to device handle
 * @return Current mode (CCS811_MODE_*)
 */
ccs811_mode_t ccs811_get_mode(ccs811_t *dev);

/**
 * @brief Check if sensor warm-up period is complete (20 minutes)
 *
 * @param dev Pointer to device handle
 * @return true if warmed up, false otherwise
 */
bool ccs811_is_warmed_up(ccs811_t *dev);

/**
 * @brief Get warm-up time remaining in seconds
 *
 * @param dev Pointer to device handle
 * @return Seconds remaining, 0 if warmed up
 */
uint32_t ccs811_warmup_seconds_remaining(ccs811_t *dev);

/**
 * @brief Set environmental data for compensation
 *
 * @param dev Pointer to device handle
 * @param temperature Temperature in Celsius
 * @param humidity Relative humidity in percent
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_set_env_data(ccs811_t *dev, float temperature, float humidity);

/**
 * @brief Software reset the sensor
 *
 * @param dev Pointer to device handle
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_reset(ccs811_t *dev);

/**
 * @brief Get error string for error code
 *
 * @param error Error code
 * @return String description of error
 */
const char* ccs811_error_string(ccs811_error_t error);

/**
 * @brief Check for hardware errors via ERROR_ID register
 *
 * @param dev Pointer to device handle
 * @return CCS811_OK if no errors, CCS811_ERR_HW_ERROR if error detected
 */
ccs811_error_t ccs811_check_error(ccs811_t *dev);

/**
 * @brief Read data with pre-validation (status check, data ready, error check)
 *
 * This is the robust read function for iteration 2+ that:
 * - Checks STATUS register before reading
 * - Validates DATA_READY flag
 * - Checks ERROR_ID register for hardware errors
 * - Uses I2C retry with exponential backoff
 *
 * @param dev Pointer to device handle
 * @param data Pointer to data structure to fill
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_read_data_robust(ccs811_t *dev, ccs811_data_t *data);

/**
 * @brief Get I2C retry statistics
 *
 * @param dev Pointer to device handle
 * @param retries Pointer to store total retry count
 * @param failures Pointer to store total failure count
 */
void ccs811_get_i2c_stats(ccs811_t *dev, uint32_t *retries, uint32_t *failures);

/**
 * @brief Read baseline value from sensor
 *
 * The baseline represents the sensor's calibration state.
 * Should be read after 20-minute warm-up period.
 *
 * @param dev Pointer to device handle
 * @param baseline Pointer to store baseline value (16-bit)
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_read_baseline(ccs811_t *dev, uint16_t *baseline);

/**
 * @brief Write baseline value to sensor
 *
 * Restores a previously saved baseline to the sensor.
 * Should only be used with baseline values < 24 hours old.
 *
 * @param dev Pointer to device handle
 * @param baseline Baseline value to write (16-bit)
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_write_baseline(ccs811_t *dev, uint16_t baseline);

#endif // CCS811_H
