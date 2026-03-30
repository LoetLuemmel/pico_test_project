/**
 * @file ccs811.c
 * @brief CCS811 Air Quality Sensor Driver Implementation
 */

#include "ccs811.h"
#include "pico/stdlib.h"
#include <string.h>

// Warm-up time in milliseconds (20 minutes)
#define CCS811_WARMUP_TIME_MS   (20 * 60 * 1000)

// I2C timeout in microseconds
#define CCS811_I2C_TIMEOUT_US   10000

/**
 * @brief Write data to a register
 */
static int ccs811_write_reg(ccs811_t *dev, uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[len + 1];
    buf[0] = reg;
    if (data && len > 0) {
        memcpy(&buf[1], data, len);
    }

    int ret = i2c_write_timeout_us(dev->i2c, dev->addr, buf, len + 1, false, CCS811_I2C_TIMEOUT_US);
    return (ret == (int)(len + 1)) ? 0 : -1;
}

/**
 * @brief Write to a register with no data (mailbox trigger)
 */
static int ccs811_write_reg_no_data(ccs811_t *dev, uint8_t reg) {
    int ret = i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, false, CCS811_I2C_TIMEOUT_US);
    return (ret == 1) ? 0 : -1;
}

/**
 * @brief Read data from a register
 */
static int ccs811_read_reg(ccs811_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    int ret = i2c_write_timeout_us(dev->i2c, dev->addr, &reg, 1, true, CCS811_I2C_TIMEOUT_US);
    if (ret != 1) {
        return -1;
    }

    ret = i2c_read_timeout_us(dev->i2c, dev->addr, data, len, false, CCS811_I2C_TIMEOUT_US);
    return (ret == (int)len) ? 0 : -1;
}

ccs811_error_t ccs811_init(ccs811_t *dev, i2c_inst_t *i2c, uint8_t addr) {
    if (!dev || !i2c) {
        return CCS811_ERR_I2C;
    }

    dev->i2c = i2c;
    dev->addr = addr;
    dev->initialized = false;
    dev->init_time_ms = 0;

    // Wait for sensor to be ready after power-on
    sleep_ms(20);

    // Step 1: Verify hardware ID
    uint8_t hw_id = ccs811_get_hw_id(dev);
    if (hw_id != CCS811_HW_ID_VALUE) {
        return CCS811_ERR_HW_ID;
    }

    // Step 2: Check if valid application firmware is loaded
    uint8_t status = ccs811_get_status(dev);
    if (!(status & CCS811_STATUS_APP_VALID)) {
        return CCS811_ERR_APP_INVALID;
    }

    // Step 3: Start application mode (write to APP_START with no data)
    if (ccs811_write_reg_no_data(dev, CCS811_REG_APP_START) != 0) {
        return CCS811_ERR_APP_START;
    }

    // Wait for app to start
    sleep_ms(1);

    // Step 4: Verify we're in application mode
    status = ccs811_get_status(dev);
    if (!(status & CCS811_STATUS_FW_MODE)) {
        return CCS811_ERR_APP_START;
    }

    // Step 5: Set measurement mode to 1 second interval
    ccs811_error_t err = ccs811_set_mode(dev, CCS811_MODE_1SEC);
    if (err != CCS811_OK) {
        return err;
    }

    dev->initialized = true;
    dev->init_time_ms = to_ms_since_boot(get_absolute_time());

    return CCS811_OK;
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
    uint8_t hw_id = 0;
    if (dev) {
        ccs811_read_reg(dev, CCS811_REG_HW_ID, &hw_id, 1);
    }
    return hw_id;
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
        case CCS811_ERR_I2C:        return "I2C communication error";
        case CCS811_ERR_HW_ID:      return "Invalid hardware ID (not 0x81)";
        case CCS811_ERR_APP_INVALID: return "No valid application firmware";
        case CCS811_ERR_APP_START:  return "Failed to start application mode";
        case CCS811_ERR_SENSOR:     return "Sensor error";
        default:                    return "Unknown error";
    }
}
