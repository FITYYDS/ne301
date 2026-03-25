/**
 * @file lsm6dsr_platform.h
 * @brief LSM6DSR platform layer header
 */

#ifndef LSM6DSR_PLATFORM_H
#define LSM6DSR_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../i2c_driver/i2c_driver.h"

/**
 * @brief Set I2C instance for platform layer
 * @param instance I2C instance pointer
 */
void lsm6dsr_platform_set_i2c_instance(i2c_instance_t *instance);

/**
 * @brief Get I2C instance from platform layer
 * @return I2C instance pointer
 */
i2c_instance_t *lsm6dsr_platform_get_i2c_instance(void);

/**
 * @brief Platform write function for LSM6DSR driver
 * @param handle Unused (device handle)
 * @param reg Register address
 * @param bufp Pointer to data buffer
 * @param len Number of bytes to write
 * @return 0 on success, negative on error
 */
int32_t lsm6dsr_platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);

/**
 * @brief Platform read function for LSM6DSR driver
 * @param handle Unused (device handle)
 * @param reg Register address
 * @param bufp Pointer to data buffer
 * @param len Number of bytes to read
 * @return 0 on success, negative on error
 */
int32_t lsm6dsr_platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);

/**
 * @brief Platform delay function for LSM6DSR driver
 * @param millisec Delay in milliseconds
 */
void lsm6dsr_platform_delay(uint32_t millisec);

#ifdef __cplusplus
}
#endif

#endif /* LSM6DSR_PLATFORM_H */
