/**
 * @file lsm6dsr_platform.c
 * @brief LSM6DSR platform layer implementation using i2c_driver
 */

#include "lsm6dsr_platform.h"
#include "../i2c_driver/i2c_driver.h"
#include "stm32n6xx_hal.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "lsm6dsr_reg.h"

#define LSM6DSR_I2C_TIMEOUT_MS    (200U)

/* Global I2C instance for LSM6DSR */
static i2c_instance_t *s_lsm6dsr_i2c_instance = NULL;

/**
 * @brief Set I2C instance for platform layer
 */
void lsm6dsr_platform_set_i2c_instance(i2c_instance_t *instance)
{
    s_lsm6dsr_i2c_instance = instance;
}

/**
 * @brief Get I2C instance from platform layer
 */
i2c_instance_t *lsm6dsr_platform_get_i2c_instance(void)
{
    return s_lsm6dsr_i2c_instance;
}

/**
 * @brief Platform write function for LSM6DSR driver
 * @param handle Unused (device handle)
 * @param reg Register address
 * @param bufp Pointer to data buffer
 * @param len Number of bytes to write
 * @return 0 on success, negative on error
 */
int32_t lsm6dsr_platform_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    int ret;
    
    (void)handle;  /* Unused parameter */
    
    if (s_lsm6dsr_i2c_instance == NULL || bufp == NULL || len == 0) {
        return -1;
    }
    
    /* LSM6DSR supports auto-increment for multi-byte writes */
    /* i2c_driver_write_reg8 handles register address + data write */
    /* Note: bufp is const, but i2c_driver_write_reg8 expects non-const, so we cast */
    ret = i2c_driver_write_reg8(s_lsm6dsr_i2c_instance, reg, (uint8_t *)bufp, len, LSM6DSR_I2C_TIMEOUT_MS);
    if (ret != (int)len) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Platform read function for LSM6DSR driver
 * @param handle Unused (device handle)
 * @param reg Register address
 * @param bufp Pointer to data buffer
 * @param len Number of bytes to read
 * @return 0 on success, negative on error
 */
int32_t lsm6dsr_platform_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    int ret;
    
    (void)handle;  /* Unused parameter */
    
    if (s_lsm6dsr_i2c_instance == NULL || bufp == NULL || len == 0) {
        return -1;
    }
    
    /* LSM6DSR supports auto-increment for multi-byte reads */
    /* i2c_driver_read_reg8 handles register address write + data read */
    ret = i2c_driver_read_reg8(s_lsm6dsr_i2c_instance, reg, bufp, len, LSM6DSR_I2C_TIMEOUT_MS);
    if (ret != (int)len) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Platform delay function for LSM6DSR driver
 * @param millisec Delay in milliseconds
 */
void lsm6dsr_platform_delay(uint32_t millisec)
{
    if (millisec > 0) {
        osDelay(millisec);
    }
}
