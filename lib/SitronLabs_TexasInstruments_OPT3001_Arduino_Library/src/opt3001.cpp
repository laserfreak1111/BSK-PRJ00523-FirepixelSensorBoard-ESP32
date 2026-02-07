/* Self header */
#include "opt3001.h"

/**
 * Reads the contents of the given register.
 * @param[in] reg_address The address of the register.
 * @param[out] reg_content A pointer to a variable that will be updated with the contents of the register.
 * @return 0 in case of success, or a negative error code otherwise.
 */
int opt3001::register_read(const enum opt3001_register reg_address, uint16_t *const reg_content) {
    int res;

    /* Ensure library has been configured */
    if (m_i2c_library == NULL) {
        return -EINVAL;
    }

    /* Send register address */
    m_i2c_library->beginTransmission(m_i2c_address);
    m_i2c_library->write(reg_address);
    res = m_i2c_library->endTransmission(false);
    if (res != 0) {
        return -EIO;
    }

    /* Read data */
    m_i2c_library->requestFrom(m_i2c_address, (uint8_t)2, (uint8_t)true);
    res = m_i2c_library->available();
    if (res == 0) {
        return -EIO;
    }
    *reg_content = m_i2c_library->read();
    *reg_content <<= 8;
    *reg_content |= m_i2c_library->read();

    /* Return success */
    return 0;
}

/**
 * Updates the content of the given register.
 * @param[in] reg_address The address of the register.
 * @param[in] reg_content The new content of the register.
 * @return 0 in case of success, or a negative error code otherwise.
 */
int opt3001::register_write(const enum opt3001_register reg_address, const uint16_t reg_content) {
    int res;

    /* Ensure library has been configured */
    if (m_i2c_library == NULL) {
        return -EINVAL;
    }

    /* Send register address and data */
    m_i2c_library->beginTransmission(m_i2c_address);
    m_i2c_library->write(reg_address);
    m_i2c_library->write((uint8_t)(reg_content >> 8));
    m_i2c_library->write((uint8_t)(reg_content >> 0));
    res = m_i2c_library->endTransmission(true);
    if (res != 0) {
        return -EIO;
    }

    /* Return success */
    return 0;
}

/**
 * Initialize the OPT3001 sensor with I2C communication parameters
 *
 * Validates the I2C address format and stores the communication parameters
 * for subsequent operations. The I2C address must match the pattern 0b01000100
 * in the upper 6 bits (addresses 0x44, 0x45, 0x46, 0x47 are valid).
 *
 * @param[in] i2c_library Reference to the TwoWire I2C library instance to use
 * @param[in] i2c_address I2C address of the OPT3001 sensor (must match 0b01000100 pattern)
 * @return 0 on success, -EINVAL if the I2C address format is invalid
 */
int opt3001::setup(TwoWire &i2c_library, const uint8_t i2c_address) {

    /* Ensure i2c address is valid */
    if ((i2c_address & 0b11111100) != 0b01000100) {
        return -EINVAL;
    }

    /* Remember i2c library and address */
    m_i2c_address = i2c_address;
    m_i2c_library = &i2c_library;

    /* Return success */
    return 0;
}

/**
 * Detect and verify the presence of an OPT3001 sensor
 *
 * Reads the manufacturer ID and device ID registers to verify that a valid
 * OPT3001 sensor is connected at the configured I2C address. The manufacturer
 * ID should be 0x5449 (Texas Instruments) and the device ID should be 0x3001.
 *
 * @return 0 if a valid OPT3001 device is detected, -EIO on I2C communication
 *         failure, or -1 if the device IDs do not match expected values
 */
int opt3001::detect(void) {
    int res;

    /* Ensure manufacturer id is as expected */
    uint16_t reg_manufacturer_id = 0x0000;
    res = register_read(OPT3001_REGISTER_MANUID, &reg_manufacturer_id);
    if (res < 0) {
        return -EIO;
    }
    if (reg_manufacturer_id != 0x5449) {
        return -1;
    }

    /* Ensure device id is as expected */
    uint16_t reg_device_id = 0x0000;
    res = register_read(OPT3001_REGISTER_DEVIID, &reg_device_id);
    if (res < 0) {
        return -EIO;
    }
    if (reg_device_id != 0x3001) {
        return -1;
    }

    /* Return success */
    return 0;
}

/**
 * Configure the sensor conversion time and enable automatic full-scale range
 *
 * Sets the conversion time (100ms or 800ms) and enables automatic full-scale
 * range selection. With automatic full-scale enabled, the sensor automatically
 * selects the optimal measurement range for the current light conditions.
 *
 * @param[in] ct Conversion time setting (OPT3001_CONVERSION_TIME_100MS or
 *                OPT3001_CONVERSION_TIME_800MS)
 * @return 0 on success, -EIO on I2C communication failure
 */
int opt3001::config_set(const enum opt3001_conversion_time ct) {
    int res;

    /* Enable automatic full scale
     * Set conversion time */
    uint16_t reg_config;
    res = register_read(OPT3001_REGISTER_CONFIG, &reg_config);
    if (res < 0) return -EIO;
    reg_config &= ~(0b1111 << 12);
    reg_config |= 0b1100 << 12;
    reg_config &= ~(0b1 << 11);
    reg_config |= (ct == OPT3001_CONVERSION_TIME_800MS) ? 0b1 : 0b0;
    res = register_write(OPT3001_REGISTER_CONFIG, reg_config);
    if (res < 0) return -EIO;

    /* Return success */
    return 0;
}

/**
 * Enable continuous conversion mode
 *
 * Configures the sensor to continuously perform conversions and update the
 * result register. The sensor will automatically start a new conversion after
 * completing the previous one. This mode provides the fastest update rate but
 * consumes more power than single-shot mode.
 *
 * @return 0 on success, -EIO on I2C communication failure
 */
int opt3001::conversion_continuous_enable(void) {
    int res;

    /* Set continuous conversion mode */
    uint16_t reg_config;
    res = register_read(OPT3001_REGISTER_CONFIG, &reg_config);
    if (res < 0) return -EIO;
    reg_config &= ~(0b11 << 9);
    reg_config |= (0b11 << 9);
    res = register_write(OPT3001_REGISTER_CONFIG, reg_config);
    if (res < 0) return -EIO;

    /* Return success */
    return 0;
}

/**
 * Disable continuous conversion mode (enter shutdown mode)
 *
 * Places the sensor in low-power shutdown state. In this mode, the sensor
 * stops performing conversions and consumes minimal power. Use this mode to
 * save power when measurements are not needed, or before triggering a
 * single-shot conversion.
 *
 * @return 0 on success, -EIO on I2C communication failure
 */
int opt3001::conversion_continuous_disable(void) {
    int res;

    /* Set shutdown conversion mode */
    uint16_t reg_config;
    res = register_read(OPT3001_REGISTER_CONFIG, &reg_config);
    if (res < 0) return -EIO;
    reg_config &= ~(0b11 << 9);
    reg_config |= (0b00 << 9);
    res = register_write(OPT3001_REGISTER_CONFIG, reg_config);
    if (res < 0) return -EIO;

    /* Return success */
    return 0;
}

/**
 * Trigger a single-shot conversion
 *
 * Initiates one conversion cycle. After the conversion completes (based on
 * the configured conversion time), the sensor automatically returns to
 * shutdown mode. This is useful for power-sensitive applications where
 * periodic measurements are sufficient. Use lux_read() after waiting for the
 * conversion time to read the result.
 *
 * @return 0 on success, -EIO on I2C communication failure
 */
int opt3001::conversion_singleshot_trigger(void) {
    int res;

    /* Set single-shot conversion mode */
    uint16_t reg_config;
    res = register_read(OPT3001_REGISTER_CONFIG, &reg_config);
    if (res < 0) return -EIO;
    reg_config &= ~(0b11 << 9);
    reg_config |= (0b01 << 9);
    res = register_write(OPT3001_REGISTER_CONFIG, reg_config);
    if (res < 0) return -EIO;

    /* Return success */
    return 0;
}

/**
 * Read the current illuminance measurement in lux
 *
 * Reads the result register and converts the raw sensor data to illuminance
 * in lux. The OPT3001 uses a mantissa-exponent format where the 12-bit
 * mantissa (bits 0-11) is multiplied by 0.01 * 2^exponent, where the
 * exponent is stored in bits 12-15. This provides a wide dynamic range
 * from 0.01 lux to 83,000 lux.
 *
 * Note: Ensure a conversion has completed before calling this function.
 * In single-shot mode, wait for the conversion time after triggering.
 * In continuous mode, the result is updated automatically.
 *
 * @param[out] lux Pointer to float variable that will receive the illuminance
 *                 value in lux
 * @return 0 on success, -EIO on I2C communication failure
 */
int opt3001::lux_read(float *const lux) {
    int res;

    /* Read result register */
    uint16_t reg_result = 0x0000;
    res = register_read(OPT3001_REGISTER_RESULT, &reg_result);
    if (res < 0) {
        return -EIO;
    }

    /* Convert to float */
#if 0
    *lux = (float)(reg_result);
#else
    uint16_t mantissa = reg_result & 0x0FFF;
    uint16_t exponent = (reg_result & 0xF000) >> 12;
    *lux = mantissa * (0.01 * pow(2, exponent));
#endif

    /* Return success */
    return 0;
}
