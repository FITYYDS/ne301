/**
 * @file vl53l1x_platform.c
 * @brief VL53L1X platform layer implementation using i2c_driver
 */

#include <string.h>
#include "vl53l1x_platform.h"
#include "../i2c_driver/i2c_driver.h"
#include "stm32n6xx_hal.h"
#include "debug.h"
#include "cmsis_os2.h"
#include "API/platform/vl53l1_platform.h"
#include "API/platform/vl53l1_types.h"

#define VL53L1X_I2C_TIMEOUT_MS    (200U)

/* Global I2C instance for VL53L1X */
static i2c_instance_t *s_vl53l1x_i2c_instance = NULL;

/**
 * @brief Set the I2C instance for VL53L1X platform functions
 * @param instance I2C instance pointer
 */
void vl53l1x_platform_set_i2c_instance(i2c_instance_t *instance)
{
    s_vl53l1x_i2c_instance = instance;
}

/**
 * @brief Get the I2C instance for VL53L1X platform functions
 * @return I2C instance pointer or NULL if not set
 */
i2c_instance_t *vl53l1x_platform_get_i2c_instance(void)
{
    return s_vl53l1x_i2c_instance;
}

/**
 * @brief VL53L1_WriteMulti implementation.
 *        Uses HAL_I2C_Mem_Write (via i2c_driver_write_reg16) which sends the
 *        16-bit register address and data in a single atomic I2C transaction.
 */
int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    int ret;

    if (s_vl53l1x_i2c_instance == NULL || pdata == NULL || count == 0) {
        return -1;
    }

    if (s_vl53l1x_i2c_instance->dev_addr != dev) {
        LOG_DRV_ERROR("vl53l1x: WriteMulti addr mismatch (inst=0x%02X dev=0x%02X)\r\n",
                      s_vl53l1x_i2c_instance->dev_addr, dev);
        return -1;
    }

    ret = i2c_driver_write_reg16(s_vl53l1x_i2c_instance, index,
                                 pdata, (uint16_t)count,
                                 VL53L1X_I2C_TIMEOUT_MS);
    return (ret == (int)count) ? 0 : -1;
}

/**
 * @brief VL53L1_ReadMulti implementation.
 *        Uses HAL_I2C_Mem_Read (via i2c_driver_read_reg16) which issues a
 *        Repeated Start between the address phase and the read phase, as
 *        required by the VL53L1X protocol.  The previous implementation used
 *        two separate Master_Transmit / Master_Receive calls with a STOP in
 *        between, which caused NACK errors and I2C bus lockups.
 */
int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    int ret;

    if (s_vl53l1x_i2c_instance == NULL || pdata == NULL || count == 0) {
        return -1;
    }

    if (s_vl53l1x_i2c_instance->dev_addr != dev) {
        LOG_DRV_ERROR("vl53l1x: ReadMulti addr mismatch (inst=0x%02X dev=0x%02X)\r\n",
                      s_vl53l1x_i2c_instance->dev_addr, dev);
        return -1;
    }

    ret = i2c_driver_read_reg16(s_vl53l1x_i2c_instance, index,
                                pdata, (uint16_t)count,
                                VL53L1X_I2C_TIMEOUT_MS);
    return (ret == (int)count) ? 0 : -1;
}

/**
 * @brief VL53L1_WrByte implementation
 */
int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data)
{
    return VL53L1_WriteMulti(dev, index, &data, 1);
}

/**
 * @brief VL53L1_WrWord implementation
 */
int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(data >> 8);  /* MSB first */
    buf[1] = (uint8_t)(data & 0xFF); /* LSB */
    return VL53L1_WriteMulti(dev, index, buf, 2);
}

/**
 * @brief VL53L1_WrDWord implementation
 */
int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)(data >> 24);
    buf[1] = (uint8_t)(data >> 16);
    buf[2] = (uint8_t)(data >> 8);
    buf[3] = (uint8_t)(data & 0xFF);
    return VL53L1_WriteMulti(dev, index, buf, 4);
}

/**
 * @brief VL53L1_RdByte implementation
 */
int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t *pdata)
{
    if (pdata == NULL) {
        return -1;
    }
    return VL53L1_ReadMulti(dev, index, pdata, 1);
}

/**
 * @brief VL53L1_RdWord implementation
 */
int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t *pdata)
{
    uint8_t buf[2] = {0, 0};
    int8_t ret;
    
    if (pdata == NULL) {
        return -1;
    }
    
    ret = VL53L1_ReadMulti(dev, index, buf, 2);
    if (ret == 0) {
        /* VL53L1X returns data in big-endian format: MSB first */
        /* buf[0] = MSB (register at index), buf[1] = LSB (register at index+1) */
        *pdata = (uint16_t)((buf[0] << 8) | buf[1]);
    }
    return ret;
}

/**
 * @brief VL53L1_RdDWord implementation
 */
int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t *pdata)
{
    uint8_t buf[4];
    int8_t ret;
    
    if (pdata == NULL) {
        return -1;
    }
    
    ret = VL53L1_ReadMulti(dev, index, buf, 4);
    if (ret == 0) {
        *pdata = (uint32_t)((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
    }
    return ret;
}

/**
 * @brief VL53L1_WaitMs implementation
 */
int8_t VL53L1_WaitMs(uint16_t dev, int32_t wait_ms)
{
    (void)dev;  /* Unused parameter */
    if (wait_ms > 0) {
        osDelay((uint32_t)wait_ms);
    }
    return 0;
}
