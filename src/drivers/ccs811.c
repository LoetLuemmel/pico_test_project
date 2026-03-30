/**
 * @file ccs811.c
 * @brief CCS811 Air Quality Sensor Driver Implementation
 */

#include "ccs811.h"
#include "pico/stdlib.h"
#include <string.h>

// I2C timeout in microseconds
#define CCS811_I2C_TIMEOUT_US   10000

// Retry delays in milliseconds (exponential backoff)
static const uint32_t retry_delays_ms[CCS811_I2C_MAX_RETRIES] = {
    CCS811_I2C_RETRY_DELAY_1_MS,
    CCS811_I2C_RETRY_DELAY_2_MS,
    CCS811_I2C_RETRY_DELAY_3_MS
};

/**
 * @brief Write data to a register (single attempt)
 */
static int ccs811_write_reg_once(ccs811_t *dev, uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[len + 1];
    buf[0] = reg;
    if (data && len > 0) {
        memcpy(&buf[1], data, len);
    }

    int ret = i2c_write_timeout_us(dev->i2c, dev->addr, buf, len + 1, false, CCS811_I2C_TIMEOUT_US);
    return (ret == (int)(len + 1)) ? 0 : -1;
}

/**
 * @brief Write data to a register with retry and exponential backoff
 */
static int ccs811_write_reg(ccs811_t *dev, uint8_t reg, const uint8_t *data, size_t len) {
    for (int attempt = 0; attempt < CCS811_I2C_MAX_RETRIES; attempt++) {
        if (ccs811_write_reg_once(dev, reg, data, len) == 0) {
            return 0;
        }
        // Retry with exponential backoff
        if (attempt < CCS811_I2C_MAX_RETRIES - 1) {
            dev->i2c_retries++;
            sleep_ms(retry_delays_ms[attempt]);
        }
    }
    dev->i2c_failures++;
    return -1;
}

/**
 * @brief Write to a register with no data (mailbox trigger) - single attempt
 */
static int ccs811_write_reg_no_data_once(ccs811_t *dev, uint8_t reg) {
    int ret = i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, false, CCS811_I2C_TIMEOUT_US);
    return (ret == 1) ? 0 : -1;
}

/**
 * @brief Write to a register with no data with retry
 */
static int ccs811_write_reg_no_data(ccs811_t *dev, uint8_t reg) {
    for (int attempt = 0; attempt < CCS811_I2C_MAX_RETRIES; attempt++) {
        if (ccs811_write_reg_no_data_once(dev, reg) == 0) {
            return 0;
        }
        if (attempt < CCS811_I2C_MAX_RETRIES - 1) {
            dev->i2c_retries++;
            sleep_ms(retry_delays_ms[attempt]);
        }
    }
    dev->i2c_failures++;
    return -1;
}

/**
 * @brief Read data from a register (single attempt)
 */
static int ccs811_read_reg_once(ccs811_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    int ret = i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, true, CCS811_I2C_TIMEOUT_US);
    if (ret != 1) {
        return -1;
    }

    ret = i2c_read_timeout_us(dev->i2c, dev->addr, data, len, false, CCS811_I2C_TIMEOUT_US);
    return (ret == (int)len) ? 0 : -1;
}

/**
 * @brief Read data from a register with retry and exponential backoff
 */
static int ccs811_read_reg(ccs811_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    for (int attempt = 0; attempt < CCS811_I2C_MAX_RETRIES; attempt++) {
        if (ccs811_read_reg_once(dev, reg, data, len) == 0) {
            return 0;
        }
        if (attempt < CCS811_I2C_MAX_RETRIES - 1) {
            dev->i2c_retries++;
            sleep_ms(retry_delays_ms[attempt]);
        }
    }
    dev->i2c_failures++;
    return -1;
}

ccs811_error_t ccs811_init_with_mode(ccs811_t *dev, i2c_inst_t *i2c, uint8_t addr, ccs811_mode_t mode) {
    if (!dev || !i2c) {
        return CCS811_ERR_I2C;
    }

    // Initialize device handle
    dev->i2c = i2c;
    dev->addr = addr;
    dev->initialized = false;
    dev->init_time_ms = 0;
    dev->i2c_retries = 0;
    dev->i2c_failures = 0;
    dev->last_error_id = 0;
    dev->last_status = 0;
    dev->hw_id = 0;
    dev->hw_version = 0;
    dev->fw_boot_version = 0;
    dev->fw_app_version = 0;
    dev->mode = CCS811_MODE_IDLE;

    // Step 1: Wait 20ms for sensor power-on stabilization
    sleep_ms(20);

    // Step 2: Verify hardware ID reads 0x81
    uint8_t hw_id = 0;
    if (ccs811_read_reg(dev, CCS811_REG_HW_ID, &hw_id, 1) != 0) {
        return CCS811_ERR_I2C;
    }
    dev->hw_id = hw_id;
    if (hw_id != CCS811_HW_ID_VALUE) {
        return CCS811_ERR_HW_ID;
    }

    // Step 3: Read hardware version
    uint8_t hw_version = 0;
    if (ccs811_read_reg(dev, CCS811_REG_HW_VERSION, &hw_version, 1) != 0) {
        return CCS811_ERR_I2C;
    }
    dev->hw_version = hw_version;

    // Step 4: Read firmware bootloader version (2 bytes)
    uint8_t fw_boot[2] = {0};
    if (ccs811_read_reg(dev, CCS811_REG_FW_BOOT_VERSION, fw_boot, 2) != 0) {
        return CCS811_ERR_I2C;
    }
    dev->fw_boot_version = ((uint16_t)fw_boot[0] << 8) | fw_boot[1];

    // Step 5: Read firmware application version (2 bytes)
    uint8_t fw_app[2] = {0};
    if (ccs811_read_reg(dev, CCS811_REG_FW_APP_VERSION, fw_app, 2) != 0) {
        return CCS811_ERR_I2C;
    }
    dev->fw_app_version = ((uint16_t)fw_app[0] << 8) | fw_app[1];

    // Step 6: Check STATUS register for APP_VALID bit
    uint8_t status = 0;
    if (ccs811_read_reg(dev, CCS811_REG_STATUS, &status, 1) != 0) {
        return CCS811_ERR_I2C;
    }
    dev->last_status = status;

    if (!(status & CCS811_STATUS_APP_VALID)) {
        return CCS811_ERR_APP_INVALID;
    }

    // Step 7: Write APP_START command to register 0xF4 (no data, just address)
    // This transitions the sensor from boot mode to application mode
    if (ccs811_write_reg_no_data(dev, CCS811_REG_APP_START) != 0) {
        return CCS811_ERR_APP_START;
    }

    // Step 8: Wait 1ms for app mode transition
    sleep_ms(1);

    // Step 9: Verify STATUS FW_MODE bit is set (now in application mode)
    if (ccs811_read_reg(dev, CCS811_REG_STATUS, &status, 1) != 0) {
        return CCS811_ERR_I2C;
    }
    dev->last_status = status;

    if (!(status & CCS811_STATUS_FW_MODE)) {
        return CCS811_ERR_APP_START;
    }

    // Step 10: Set the requested measurement mode
    ccs811_error_t err = ccs811_set_mode(dev, mode);
    if (err != CCS811_OK) {
        return err;
    }
    dev->mode = mode;

    // Initialization complete
    dev->initialized = true;
    dev->init_time_ms = to_ms_since_boot(get_absolute_time());

    return CCS811_OK;
}

ccs811_error_t ccs811_init(ccs811_t *dev, i2c_inst_t *i2c, uint8_t addr) {
    // Default to Mode 1 (1 second measurement interval)
    return ccs811_init_with_mode(dev, i2c, addr, CCS811_MODE_1SEC);
}

ccs811_error_t ccs811_set_mode(ccs811_t *dev, ccs811_mode_t mode) {
    if (!dev) {
        return CCS811_ERR_I2C;
    }

    // MEAS_MODE register: bits 6:4 = DRIVE_MODE
    uint8_t meas_mode = (mode & 0x07) << 4;

    if (ccs811_write_reg(dev, CCS811_REG_MEAS_MODE, &meas_mode, 1) != 0) {
        return CCS811_ERR_I2C;
    }

    return CCS811_OK;
}

bool ccs811_data_ready(ccs811_t *dev) {
    if (!dev || !dev->initialized) {
        return false;
    }

    uint8_t status = ccs811_get_status(dev);
    return (status & CCS811_STATUS_DATA_READY) != 0;
}

ccs811_error_t ccs811_read_data(ccs811_t *dev, ccs811_data_t *data) {
    if (!dev || !data) {
        return CCS811_ERR_I2C;
    }

    // Read 8 bytes from ALG_RESULT_DATA register
    // Bytes: eCO2[MSB], eCO2[LSB], TVOC[MSB], TVOC[LSB], STATUS, ERROR_ID, RAW[MSB], RAW[LSB]
    uint8_t buf[8];
    if (ccs811_read_reg(dev, CCS811_REG_ALG_RESULT_DATA, buf, 8) != 0) {
        return CCS811_ERR_I2C;
    }

    // Parse data (MSB first)
    data->eco2 = ((uint16_t)buf[0] << 8) | buf[1];
    data->tvoc = ((uint16_t)buf[2] << 8) | buf[3];
    data->status = buf[4];
    data->error_id = buf[5];

    data->data_ready = (data->status & CCS811_STATUS_DATA_READY) != 0;
    data->error = (data->status & CCS811_STATUS_ERROR) != 0;

    // Check for sensor errors
    if (data->error) {
        return CCS811_ERR_SENSOR;
    }

    return CCS811_OK;
}

uint8_t ccs811_get_status(ccs811_t *dev) {
    uint8_t status = 0;
    if (dev) {
        ccs811_read_reg(dev, CCS811_REG_STATUS, &status, 1);
    }
    return status;
}

uint8_t ccs811_get_error_id(ccs811_t *dev) {
    uint8_t error_id = 0;
    if (dev) {
        ccs811_read_reg(dev, CCS811_REG_ERROR_ID, &error_id, 1);
    }
    return error_id;
}

uint8_t ccs811_get_hw_id(ccs811_t *dev) {
    if (dev && dev->initialized) {
        return dev->hw_id;  // Return cached value
    }
    // Fallback: read from device
    uint8_t hw_id = 0;
    if (dev) {
        ccs811_read_reg(dev, CCS811_REG_HW_ID, &hw_id, 1);
    }
    return hw_id;
}

uint8_t ccs811_get_hw_version(ccs811_t *dev) {
    return dev ? dev->hw_version : 0;
}

uint16_t ccs811_get_fw_boot_version(ccs811_t *dev) {
    return dev ? dev->fw_boot_version : 0;
}

uint16_t ccs811_get_fw_app_version(ccs811_t *dev) {
    return dev ? dev->fw_app_version : 0;
}

ccs811_mode_t ccs811_get_mode(ccs811_t *dev) {
    return dev ? dev->mode : CCS811_MODE_IDLE;
}

bool ccs811_is_warmed_up(ccs811_t *dev) {
    if (!dev || !dev->initialized) {
        return false;
    }

    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - dev->init_time_ms;
    return elapsed >= CCS811_WARMUP_TIME_MS;
}

uint32_t ccs811_warmup_seconds_remaining(ccs811_t *dev) {
    if (!dev || !dev->initialized) {
        return CCS811_WARMUP_TIME_MS / 1000;
    }

    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - dev->init_time_ms;
    if (elapsed >= CCS811_WARMUP_TIME_MS) {
        return 0;
    }

    return (CCS811_WARMUP_TIME_MS - elapsed) / 1000;
}

ccs811_error_t ccs811_set_env_data(ccs811_t *dev, float temperature, float humidity) {
    if (!dev) {
        return CCS811_ERR_I2C;
    }

    // Convert to CCS811 format
    // Humidity: value * 512 (0.5% resolution)
    // Temperature: (value + 25) * 512 (0.5°C resolution, offset by 25°C)
    uint16_t hum_conv = (uint16_t)(humidity * 512.0f);
    uint16_t temp_conv = (uint16_t)((temperature + 25.0f) * 512.0f);

    uint8_t env_data[4] = {
        (uint8_t)(hum_conv >> 8),
        (uint8_t)(hum_conv & 0xFF),
        (uint8_t)(temp_conv >> 8),
        (uint8_t)(temp_conv & 0xFF)
    };

    if (ccs811_write_reg(dev, CCS811_REG_ENV_DATA, env_data, 4) != 0) {
        return CCS811_ERR_I2C;
    }

    return CCS811_OK;
}

ccs811_error_t ccs811_reset(ccs811_t *dev) {
    if (!dev) {
        return CCS811_ERR_I2C;
    }

    // Reset sequence: write 0x11 0xE5 0x72 0x8A to SW_RESET register
    uint8_t reset_seq[4] = {0x11, 0xE5, 0x72, 0x8A};

    if (ccs811_write_reg(dev, CCS811_REG_SW_RESET, reset_seq, 4) != 0) {
        return CCS811_ERR_I2C;
    }

    dev->initialized = false;
    sleep_ms(20);  // Wait for reset

    return CCS811_OK;
}

const char* ccs811_error_string(ccs811_error_t error) {
    switch (error) {
        case CCS811_OK:             return "OK";
        case CCS811_ERR_I2C:        return "I2C_ERROR";
        case CCS811_ERR_HW_ID:      return "HW_ID_INVALID";
        case CCS811_ERR_APP_INVALID: return "APP_INVALID";
        case CCS811_ERR_APP_START:  return "APP_START_FAIL";
        case CCS811_ERR_SENSOR:     return "SENSOR_ERROR";
        case CCS811_ERR_I2C_TIMEOUT: return "I2C_TIMEOUT";
        case CCS811_ERR_NOT_READY:  return "NOT_READY";
        case CCS811_ERR_HW_ERROR:   return "HW_ERROR";
        default:                    return "UNKNOWN";
    }
}

ccs811_error_t ccs811_check_error(ccs811_t *dev) {
    if (!dev) {
        return CCS811_ERR_I2C;
    }

    // Read STATUS register first
    uint8_t status = 0;
    if (ccs811_read_reg(dev, CCS811_REG_STATUS, &status, 1) != 0) {
        return CCS811_ERR_I2C_TIMEOUT;
    }
    dev->last_status = status;

    // Check if error bit is set
    if (status & CCS811_STATUS_ERROR) {
        // Read ERROR_ID register for details
        uint8_t error_id = 0;
        if (ccs811_read_reg(dev, CCS811_REG_ERROR_ID, &error_id, 1) != 0) {
            return CCS811_ERR_I2C_TIMEOUT;
        }
        dev->last_error_id = error_id;
        return CCS811_ERR_HW_ERROR;
    }

    return CCS811_OK;
}

ccs811_error_t ccs811_read_data_robust(ccs811_t *dev, ccs811_data_t *data) {
    if (!dev || !data) {
        return CCS811_ERR_I2C;
    }

    // Step 1: Read and check STATUS register
    uint8_t status = 0;
    if (ccs811_read_reg(dev, CCS811_REG_STATUS, &status, 1) != 0) {
        return CCS811_ERR_I2C_TIMEOUT;
    }
    dev->last_status = status;

    // Step 2: Check for hardware errors (ERROR bit in STATUS)
    if (status & CCS811_STATUS_ERROR) {
        // Read ERROR_ID register (0xE0) for details
        uint8_t error_id = 0;
        ccs811_read_reg(dev, CCS811_REG_ERROR_ID, &error_id, 1);
        dev->last_error_id = error_id;
        data->error = true;
        data->error_id = error_id;
        data->status = status;
        return CCS811_ERR_HW_ERROR;
    }

    // Step 3: Validate DATA_READY flag
    if (!(status & CCS811_STATUS_DATA_READY)) {
        data->data_ready = false;
        data->status = status;
        return CCS811_ERR_NOT_READY;
    }

    // Step 4: Read ALG_RESULT_DATA (8 bytes)
    uint8_t buf[8];
    if (ccs811_read_reg(dev, CCS811_REG_ALG_RESULT_DATA, buf, 8) != 0) {
        return CCS811_ERR_I2C_TIMEOUT;
    }

    // Parse data (MSB first)
    data->eco2 = ((uint16_t)buf[0] << 8) | buf[1];
    data->tvoc = ((uint16_t)buf[2] << 8) | buf[3];
    data->status = buf[4];
    data->error_id = buf[5];
    data->data_ready = true;
    data->error = (data->status & CCS811_STATUS_ERROR) != 0;

    // Step 5: Final error check from result data
    if (data->error) {
        dev->last_error_id = data->error_id;
        return CCS811_ERR_SENSOR;
    }

    return CCS811_OK;
}

void ccs811_get_i2c_stats(ccs811_t *dev, uint32_t *retries, uint32_t *failures) {
    if (dev) {
        if (retries) *retries = dev->i2c_retries;
        if (failures) *failures = dev->i2c_failures;
    }
}

ccs811_error_t ccs811_read_baseline(ccs811_t *dev, uint16_t *baseline) {
    if (!dev || !dev->initialized || !baseline) {
        return CCS811_ERR_I2C;
    }

    uint8_t buf[2];
    if (ccs811_read_reg(dev, CCS811_REG_BASELINE, buf, 2) != 0) {
        return CCS811_ERR_I2C;
    }

    // Baseline is stored MSB first
    *baseline = ((uint16_t)buf[0] << 8) | buf[1];
    return CCS811_OK;
}

ccs811_error_t ccs811_write_baseline(ccs811_t *dev, uint16_t baseline) {
    if (!dev || !dev->initialized) {
        return CCS811_ERR_I2C;
    }

    // Baseline is written MSB first
    uint8_t buf[2];
    buf[0] = (baseline >> 8) & 0xFF;
    buf[1] = baseline & 0xFF;

    if (ccs811_write_reg(dev, CCS811_REG_BASELINE, buf, 2) != 0) {
        return CCS811_ERR_I2C;
    }

    return CCS811_OK;
}
