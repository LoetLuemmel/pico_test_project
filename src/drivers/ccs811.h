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
    CCS811_ERR_SENSOR       = -5
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

// Device handle
typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    bool initialized;
    uint32_t init_time_ms;  // Time when sensor was initialized (for warm-up tracking)
} ccs811_t;

/**
 * @brief Initialize the CCS811 sensor
 *
 * @param dev Pointer to device handle
 * @param i2c I2C instance (i2c0 or i2c1)
 * @param addr I2C address (0x5A or 0x5B)
 * @return CCS811_OK on success, error code on failure
 */
ccs811_error_t ccs811_init(ccs811_t *dev, i2c_inst_t *i2c, uint8_t addr);

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

#endif // CCS811_H
