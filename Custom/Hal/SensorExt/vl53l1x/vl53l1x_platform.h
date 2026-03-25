/**
 * @file vl53l1x_platform.h
 * @brief VL53L1X platform layer header
 */

#ifndef VL53L1X_PLATFORM_H
#define VL53L1X_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../i2c_driver/i2c_driver.h"

/**
 * @brief Set the I2C instance for VL53L1X platform functions
 * @param instance I2C instance pointer
 */
void vl53l1x_platform_set_i2c_instance(i2c_instance_t *instance);

/**
 * @brief Get the I2C instance for VL53L1X platform functions
 * @return I2C instance pointer or NULL if not set
 */
i2c_instance_t *vl53l1x_platform_get_i2c_instance(void);

#ifdef __cplusplus
}
#endif

#endif /* VL53L1X_PLATFORM_H */
