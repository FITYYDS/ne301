/**
 * @file lsm6dsr.c
 * @brief LSM6DSR 6-axis IMU sensor driver implementation
 */

#include <stddef.h>
#include <string.h>
#include "lsm6dsr.h"
#include "lsm6dsr_platform.h"
#include "aicam_types.h"
#include "debug.h"
#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "../i2c_driver/i2c_driver.h"
#include "lsm6dsr_reg.h"

#define LSM6DSR_I2C_TIMEOUT_MS    (200U)
#define LSM6DSR_BOOT_TIME_MS      (10U)

static i2c_instance_t *s_lsm6dsr_instance = NULL;
static stmdev_ctx_t s_lsm6dsr_ctx;
static lsm6dsr_fs_xl_t s_acc_fs = LSM6DSR_2g;
static lsm6dsr_fs_g_t s_gyro_fs = LSM6DSR_2000dps;

/**
 * @brief Convert ODR enum to driver ODR value
 */
static lsm6dsr_odr_xl_t lsm6dsr_odr_to_xl_odr(lsm6dsr_odr_t odr)
{
    switch (odr) {
        case LSM6DSR_ODR_POWER_DOWN: return LSM6DSR_XL_ODR_OFF;
        case LSM6DSR_ODR_12Hz5: return LSM6DSR_XL_ODR_12Hz5;
        case LSM6DSR_ODR_26Hz: return LSM6DSR_XL_ODR_26Hz;
        case LSM6DSR_ODR_52Hz: return LSM6DSR_XL_ODR_52Hz;
        case LSM6DSR_ODR_104Hz: return LSM6DSR_XL_ODR_104Hz;
        case LSM6DSR_ODR_208Hz: return LSM6DSR_XL_ODR_208Hz;
        case LSM6DSR_ODR_416Hz: return LSM6DSR_XL_ODR_416Hz;
        case LSM6DSR_ODR_833Hz: return LSM6DSR_XL_ODR_833Hz;
        case LSM6DSR_ODR_1666Hz: return LSM6DSR_XL_ODR_1666Hz;
        case LSM6DSR_ODR_3332Hz: return LSM6DSR_XL_ODR_3332Hz;
        case LSM6DSR_ODR_6664Hz: return LSM6DSR_XL_ODR_6667Hz;
        default: return LSM6DSR_XL_ODR_OFF;
    }
}

/**
 * @brief Convert ODR enum to driver gyro ODR value
 */
static lsm6dsr_odr_g_t lsm6dsr_odr_to_gy_odr(lsm6dsr_odr_t odr)
{
    switch (odr) {
        case LSM6DSR_ODR_POWER_DOWN: return LSM6DSR_GY_ODR_OFF;
        case LSM6DSR_ODR_12Hz5: return LSM6DSR_GY_ODR_12Hz5;
        case LSM6DSR_ODR_26Hz: return LSM6DSR_GY_ODR_26Hz;
        case LSM6DSR_ODR_52Hz: return LSM6DSR_GY_ODR_52Hz;
        case LSM6DSR_ODR_104Hz: return LSM6DSR_GY_ODR_104Hz;
        case LSM6DSR_ODR_208Hz: return LSM6DSR_GY_ODR_208Hz;
        case LSM6DSR_ODR_416Hz: return LSM6DSR_GY_ODR_416Hz;
        case LSM6DSR_ODR_833Hz: return LSM6DSR_GY_ODR_833Hz;
        case LSM6DSR_ODR_1666Hz: return LSM6DSR_GY_ODR_1666Hz;
        case LSM6DSR_ODR_3332Hz: return LSM6DSR_GY_ODR_3332Hz;
        case LSM6DSR_ODR_6664Hz: return LSM6DSR_GY_ODR_6667Hz;
        default: return LSM6DSR_GY_ODR_OFF;
    }
}

/**
 * @brief Convert acc FS enum to driver FS value
 */
static lsm6dsr_fs_xl_t lsm6dsr_acc_fs_to_driver(lsm6dsr_acc_fs_t fs)
{
    switch (fs) {
        case LSM6DSR_ACC_FS_2G: return LSM6DSR_2g;
        case LSM6DSR_ACC_FS_4G: return LSM6DSR_4g;
        case LSM6DSR_ACC_FS_8G: return LSM6DSR_8g;
        case LSM6DSR_ACC_FS_16G: return LSM6DSR_16g;
        default: return LSM6DSR_2g;
    }
}

/**
 * @brief Convert gyro FS enum to driver FS value
 */
static lsm6dsr_fs_g_t lsm6dsr_gyro_fs_to_driver(lsm6dsr_gyro_fs_t fs)
{
    switch (fs) {
        case LSM6DSR_GYRO_FS_125DPS: return LSM6DSR_125dps;
        case LSM6DSR_GYRO_FS_250DPS: return LSM6DSR_250dps;
        case LSM6DSR_GYRO_FS_500DPS: return LSM6DSR_500dps;
        case LSM6DSR_GYRO_FS_1000DPS: return LSM6DSR_1000dps;
        case LSM6DSR_GYRO_FS_2000DPS: return LSM6DSR_2000dps;
        default: return LSM6DSR_2000dps;
    }
}

int lsm6dsr_init(uint8_t i2c_addr)
{
    int ret;
    uint8_t device_id = 0;
    uint8_t rst = 1;
    
    if (s_lsm6dsr_instance != NULL) {
        LOG_DRV_WARN("lsm6dsr: Already initialized\r\n");
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    /* Create I2C instance */
    s_lsm6dsr_instance = i2c_driver_create(I2C_PORT_1,
                                           (uint16_t)(i2c_addr << 1),
                                           I2C_ADDRESS_7BIT);
    if (s_lsm6dsr_instance == NULL) {
        LOG_DRV_ERROR("lsm6dsr: i2c_driver_create failed\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    /* Check if device is ready */
    ret = i2c_driver_is_ready(s_lsm6dsr_instance, 3, 200);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("lsm6dsr: Device not ready at address 0x%02X\r\n", i2c_addr);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }
    
    /* Set platform I2C instance */
    lsm6dsr_platform_set_i2c_instance(s_lsm6dsr_instance);
    
    /* Initialize driver context */
    s_lsm6dsr_ctx.write_reg = lsm6dsr_platform_write;
    s_lsm6dsr_ctx.read_reg = lsm6dsr_platform_read;
    s_lsm6dsr_ctx.mdelay = lsm6dsr_platform_delay;
    s_lsm6dsr_ctx.handle = NULL;
    
    /* Wait for boot time */
    osDelay(LSM6DSR_BOOT_TIME_MS);
    
    /* Check device ID */
    if (lsm6dsr_device_id_get(&s_lsm6dsr_ctx, &device_id) != 0) {
        LOG_DRV_ERROR("lsm6dsr: Cannot read device ID\r\n");
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    if (device_id != LSM6DSR_DEVICE_ID) {
        LOG_DRV_ERROR("lsm6dsr: Device ID mismatch (got 0x%02X, expected 0x%02X)\r\n",
                     device_id, LSM6DSR_DEVICE_ID);
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    /* Reset device */
    if (lsm6dsr_reset_set(&s_lsm6dsr_ctx, PROPERTY_ENABLE) != 0) {
        LOG_DRV_ERROR("lsm6dsr: Reset failed\r\n");
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    /* Wait for reset to complete */
    do {
        if (lsm6dsr_reset_get(&s_lsm6dsr_ctx, &rst) != 0) {
            LOG_DRV_ERROR("lsm6dsr: Reset get failed\r\n");
            lsm6dsr_platform_set_i2c_instance(NULL);
            i2c_driver_destroy(s_lsm6dsr_instance);
            s_lsm6dsr_instance = NULL;
            return AICAM_ERROR_PROTOCOL;
        }
        osDelay(1);
    } while (rst);
    
    /* Disable I3C interface */
    lsm6dsr_i3c_disable_set(&s_lsm6dsr_ctx, LSM6DSR_I3C_DISABLE);
    
    /* Enable Block Data Update */
    lsm6dsr_block_data_update_set(&s_lsm6dsr_ctx, PROPERTY_ENABLE);
    
    /* Set default output data rate and full scale */
    /* Accelerometer: 12.5 Hz, ±2g */
    if (lsm6dsr_xl_data_rate_set(&s_lsm6dsr_ctx, LSM6DSR_XL_ODR_12Hz5) != 0) {
        LOG_DRV_ERROR("lsm6dsr: Failed to set accelerometer ODR\r\n");
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    if (lsm6dsr_xl_full_scale_set(&s_lsm6dsr_ctx, LSM6DSR_2g) != 0) {
        LOG_DRV_ERROR("lsm6dsr: Failed to set accelerometer full scale\r\n");
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    s_acc_fs = LSM6DSR_2g;
    
    /* Gyroscope: 12.5 Hz, ±2000 dps */
    if (lsm6dsr_gy_data_rate_set(&s_lsm6dsr_ctx, LSM6DSR_GY_ODR_12Hz5) != 0) {
        LOG_DRV_ERROR("lsm6dsr: Failed to set gyroscope ODR\r\n");
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    if (lsm6dsr_gy_full_scale_set(&s_lsm6dsr_ctx, LSM6DSR_2000dps) != 0) {
        LOG_DRV_ERROR("lsm6dsr: Failed to set gyroscope full scale\r\n");
        lsm6dsr_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_lsm6dsr_instance);
        s_lsm6dsr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    s_gyro_fs = LSM6DSR_2000dps;
    
    LOG_DRV_INFO("lsm6dsr: init OK (addr=0x%02X, id=0x%02X, default: acc=12.5Hz/±2g, gyro=12.5Hz/±2000dps)\r\n", 
                 i2c_addr, device_id);
    
    return AICAM_OK;
}

void lsm6dsr_deinit(void)
{
    if (s_lsm6dsr_instance == NULL) {
        return;
    }
    
    lsm6dsr_platform_set_i2c_instance(NULL);
    i2c_driver_destroy(s_lsm6dsr_instance);
    s_lsm6dsr_instance = NULL;
}

bool lsm6dsr_is_initialized(void)
{
    return (s_lsm6dsr_instance != NULL);
}

int lsm6dsr_reset(void)
{
    uint8_t rst = 1;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (lsm6dsr_reset_set(&s_lsm6dsr_ctx, PROPERTY_ENABLE) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    /* Wait for reset to complete */
    do {
        if (lsm6dsr_reset_get(&s_lsm6dsr_ctx, &rst) != 0) {
            return AICAM_ERROR_PROTOCOL;
        }
        osDelay(1);
    } while (rst);
    
    return AICAM_OK;
}

int lsm6dsr_acc_config(lsm6dsr_odr_t odr, lsm6dsr_acc_fs_t fs)
{
    lsm6dsr_odr_xl_t xl_odr;
    lsm6dsr_fs_xl_t xl_fs;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    xl_odr = lsm6dsr_odr_to_xl_odr(odr);
    xl_fs = lsm6dsr_acc_fs_to_driver(fs);
    
    if (lsm6dsr_xl_data_rate_set(&s_lsm6dsr_ctx, xl_odr) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    if (lsm6dsr_xl_full_scale_set(&s_lsm6dsr_ctx, xl_fs) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    s_acc_fs = xl_fs;
    
    return AICAM_OK;
}

int lsm6dsr_gyro_config(lsm6dsr_odr_t odr, lsm6dsr_gyro_fs_t fs)
{
    lsm6dsr_odr_g_t gy_odr;
    lsm6dsr_fs_g_t gy_fs;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    gy_odr = lsm6dsr_odr_to_gy_odr(odr);
    gy_fs = lsm6dsr_gyro_fs_to_driver(fs);
    
    if (lsm6dsr_gy_data_rate_set(&s_lsm6dsr_ctx, gy_odr) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    if (lsm6dsr_gy_full_scale_set(&s_lsm6dsr_ctx, gy_fs) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    s_gyro_fs = gy_fs;
    
    return AICAM_OK;
}

int lsm6dsr_read_data(lsm6dsr_data_t *data)
{
    int16_t raw_acc[3] = {0};
    int16_t raw_gyro[3] = {0};
    int16_t raw_temp = 0;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (data == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    /* Read acceleration */
    if (lsm6dsr_acceleration_raw_get(&s_lsm6dsr_ctx, raw_acc) != 0) {
        return AICAM_ERROR_IO;
    }
    
    /* Read angular rate */
    if (lsm6dsr_angular_rate_raw_get(&s_lsm6dsr_ctx, raw_gyro) != 0) {
        return AICAM_ERROR_IO;
    }
    
    /* Read temperature */
    if (lsm6dsr_temperature_raw_get(&s_lsm6dsr_ctx, &raw_temp) != 0) {
        return AICAM_ERROR_IO;
    }
    
    /* Convert acceleration based on full scale */
    switch (s_acc_fs) {
        case LSM6DSR_2g:
            data->acc_x = lsm6dsr_from_fs2g_to_mg(raw_acc[0]);
            data->acc_y = lsm6dsr_from_fs2g_to_mg(raw_acc[1]);
            data->acc_z = lsm6dsr_from_fs2g_to_mg(raw_acc[2]);
            break;
        case LSM6DSR_4g:
            data->acc_x = lsm6dsr_from_fs4g_to_mg(raw_acc[0]);
            data->acc_y = lsm6dsr_from_fs4g_to_mg(raw_acc[1]);
            data->acc_z = lsm6dsr_from_fs4g_to_mg(raw_acc[2]);
            break;
        case LSM6DSR_8g:
            data->acc_x = lsm6dsr_from_fs8g_to_mg(raw_acc[0]);
            data->acc_y = lsm6dsr_from_fs8g_to_mg(raw_acc[1]);
            data->acc_z = lsm6dsr_from_fs8g_to_mg(raw_acc[2]);
            break;
        case LSM6DSR_16g:
            data->acc_x = lsm6dsr_from_fs16g_to_mg(raw_acc[0]);
            data->acc_y = lsm6dsr_from_fs16g_to_mg(raw_acc[1]);
            data->acc_z = lsm6dsr_from_fs16g_to_mg(raw_acc[2]);
            break;
        default:
            data->acc_x = lsm6dsr_from_fs2g_to_mg(raw_acc[0]);
            data->acc_y = lsm6dsr_from_fs2g_to_mg(raw_acc[1]);
            data->acc_z = lsm6dsr_from_fs2g_to_mg(raw_acc[2]);
            break;
    }
    
    /* Convert angular rate based on full scale */
    switch (s_gyro_fs) {
        case LSM6DSR_125dps:
            data->gyro_x = lsm6dsr_from_fs125dps_to_mdps(raw_gyro[0]);
            data->gyro_y = lsm6dsr_from_fs125dps_to_mdps(raw_gyro[1]);
            data->gyro_z = lsm6dsr_from_fs125dps_to_mdps(raw_gyro[2]);
            break;
        case LSM6DSR_250dps:
            data->gyro_x = lsm6dsr_from_fs250dps_to_mdps(raw_gyro[0]);
            data->gyro_y = lsm6dsr_from_fs250dps_to_mdps(raw_gyro[1]);
            data->gyro_z = lsm6dsr_from_fs250dps_to_mdps(raw_gyro[2]);
            break;
        case LSM6DSR_500dps:
            data->gyro_x = lsm6dsr_from_fs500dps_to_mdps(raw_gyro[0]);
            data->gyro_y = lsm6dsr_from_fs500dps_to_mdps(raw_gyro[1]);
            data->gyro_z = lsm6dsr_from_fs500dps_to_mdps(raw_gyro[2]);
            break;
        case LSM6DSR_1000dps:
            data->gyro_x = lsm6dsr_from_fs1000dps_to_mdps(raw_gyro[0]);
            data->gyro_y = lsm6dsr_from_fs1000dps_to_mdps(raw_gyro[1]);
            data->gyro_z = lsm6dsr_from_fs1000dps_to_mdps(raw_gyro[2]);
            break;
        case LSM6DSR_2000dps:
            data->gyro_x = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[0]);
            data->gyro_y = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[1]);
            data->gyro_z = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[2]);
            break;
        default:
            data->gyro_x = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[0]);
            data->gyro_y = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[1]);
            data->gyro_z = lsm6dsr_from_fs2000dps_to_mdps(raw_gyro[2]);
            break;
    }
    
    /* Convert temperature */
    data->temperature = lsm6dsr_from_lsb_to_celsius(raw_temp);
    
    return AICAM_OK;
}

int lsm6dsr_acc_data_ready(bool *ready)
{
    uint8_t reg = 0;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (ready == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (lsm6dsr_xl_flag_data_ready_get(&s_lsm6dsr_ctx, &reg) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    *ready = (reg != 0);
    
    return AICAM_OK;
}

int lsm6dsr_gyro_data_ready(bool *ready)
{
    uint8_t reg = 0;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (ready == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (lsm6dsr_gy_flag_data_ready_get(&s_lsm6dsr_ctx, &reg) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    *ready = (reg != 0);
    
    return AICAM_OK;
}

int lsm6dsr_temp_data_ready(bool *ready)
{
    uint8_t reg = 0;
    
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (ready == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (lsm6dsr_temp_flag_data_ready_get(&s_lsm6dsr_ctx, &reg) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    *ready = (reg != 0);
    
    return AICAM_OK;
}

int lsm6dsr_get_device_id(uint8_t *id)
{
    if (s_lsm6dsr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (id == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (lsm6dsr_device_id_get(&s_lsm6dsr_ctx, id) != 0) {
        return AICAM_ERROR_PROTOCOL;
    }
    
    return AICAM_OK;
}
