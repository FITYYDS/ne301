/**
 * @file vl53l1x.c
 * @brief VL53L1X ToF ranging sensor driver implementation
 */

#include <stddef.h>
#include <string.h>
#include "vl53l1x.h"
#include "vl53l1x_platform.h"
#include "aicam_types.h"
#include "debug.h"
#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "../i2c_driver/i2c_driver.h"
#include "API/core/VL53L1X_api.h"

static i2c_instance_t *s_vl53l1x_instance = NULL;
static uint16_t s_vl53l1x_dev_addr = 0;  /* 8-bit I2C address for API calls (e.g., 0x52) */

int vl53l1x_init(uint8_t i2c_addr)
{
    int ret;
    uint8_t boot_state = 0;
    uint16_t sensor_id = 0;
    VL53L1X_ERROR api_status;
    
    if (i2c_addr == 0) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (s_vl53l1x_instance != NULL) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    /* I2C port init/deinit is caller's responsibility; only create instance here. */
    /* 7-bit address -> HAL 8-bit address (addr << 1) */
    s_vl53l1x_instance = i2c_driver_create(I2C_PORT_1,
                                           (uint16_t)(i2c_addr << 1),
                                           I2C_ADDRESS_7BIT);
    if (s_vl53l1x_instance == NULL) {
        LOG_DRV_ERROR("vl53l1x: i2c_driver_create failed (ensure I2C port is inited)\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    /* Quick check if device is ready (fast pre-check before setting up platform) */
    ret = i2c_driver_is_ready(s_vl53l1x_instance, 3, 200);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("vl53l1x: device not ready at address 0x%02X\r\n", i2c_addr);
        i2c_driver_destroy(s_vl53l1x_instance);
        s_vl53l1x_instance = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }
    
    /* Set platform I2C instance (required for API calls) */
    vl53l1x_platform_set_i2c_instance(s_vl53l1x_instance);
    /* Store 8-bit address for API calls (API expects 8-bit address like 0x52) */
    s_vl53l1x_dev_addr = (uint16_t)(i2c_addr << 1);  /* Convert 7-bit to 8-bit address */
    
    /* Try to read Model ID register (0x010F) to verify device exists */
    /* This works even if device is not fully booted */
    uint8_t model_id_byte = 0;
    api_status = VL53L1_RdByte(s_vl53l1x_dev_addr, 0x010F, &model_id_byte);
    if (api_status != 0) {
        LOG_DRV_ERROR("vl53l1x: Cannot read Model ID at address 0x%02X (device not responding)\r\n", i2c_addr);
        vl53l1x_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_vl53l1x_instance);
        s_vl53l1x_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    /* Wait for boot state - device may need time to boot */
    uint16_t timeout = 1000;
    while (boot_state == 0 && timeout > 0) {
        api_status = VL53L1X_BootState(s_vl53l1x_dev_addr, &boot_state);
        if (api_status != 0) {
            /* If read fails, device might not be present or not responding */
            osDelay(2);
            timeout--;
            continue;
        }
        if (boot_state == 0) {
            osDelay(2);
            timeout--;
        }
    }
    
    if (boot_state == 0) {
        LOG_DRV_ERROR("vl53l1x: boot timeout (device may need reset or more time to boot)\r\n");
        vl53l1x_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_vl53l1x_instance);
        s_vl53l1x_instance = NULL;
        return AICAM_ERROR_TIMEOUT;
    }
    
    /* Verify sensor ID - read Word from 0x010F */
    /* According to datasheet: 0x010F = Model ID (0xEA), 0x0110 = Module Type (0xAC) */
    /* Reading Word from 0x010F should give 0xEEAC (but we're getting 0xEACC) */
    api_status = VL53L1X_GetSensorId(s_vl53l1x_dev_addr, &sensor_id);
    if (api_status != 0) {
        LOG_DRV_ERROR("vl53l1x: GetSensorId failed (status=%d)\r\n", api_status);
        vl53l1x_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_vl53l1x_instance);
        s_vl53l1x_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    /* Check sensor ID - Model ID byte should be 0xEA */
    /* Accept 0xEEAC (standard) or 0xEACC (if Module Type is 0xCC instead of 0xAC) */
    if ((sensor_id & 0xFF00) != 0xEA00) {
        LOG_DRV_ERROR("vl53l1x: Sensor ID mismatch (got 0x%04X, Model ID should be 0xEA)\r\n", sensor_id);
        LOG_DRV_ERROR("vl53l1x: Device at 0x%02X may not be VL53L1X\r\n", i2c_addr);
        vl53l1x_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_vl53l1x_instance);
        s_vl53l1x_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    /* Accept sensor ID if Model ID is correct (0xEA) */
    /* Some variants may have different Module Type values */
    if (sensor_id == 0xEEAC) {
        LOG_DRV_DEBUG("vl53l1x: Sensor ID = 0x%04X (standard)\r\n", sensor_id);
    } else if ((sensor_id & 0xFF00) == 0xEA00) {
        LOG_DRV_INFO("vl53l1x: Sensor ID = 0x%04X (Model ID 0xEA verified, continuing)\r\n", sensor_id);
    } else {
        LOG_DRV_WARN("vl53l1x: Sensor ID is 0x%04X (unexpected, but Model ID is 0xEA, continuing)\r\n", sensor_id);
    }
    
    /* Initialize sensor */
    api_status = VL53L1X_SensorInit(s_vl53l1x_dev_addr);
    if (api_status != 0) {
        LOG_DRV_ERROR("vl53l1x: SensorInit failed %d\r\n", api_status);
        vl53l1x_platform_set_i2c_instance(NULL);
        i2c_driver_destroy(s_vl53l1x_instance);
        s_vl53l1x_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }
    
    LOG_DRV_INFO("vl53l1x: init OK (addr=0x%02X, sensor_id=0x%04X)\r\n", i2c_addr, sensor_id);
    return AICAM_OK;
}

int vl53l1x_start_ranging(void)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_StartRanging(s_vl53l1x_dev_addr);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_stop_ranging(void)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_StopRanging(s_vl53l1x_dev_addr);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_check_data_ready(bool *is_ready)
{
    uint8_t ready;
    VL53L1X_ERROR api_status;
    
    if (is_ready == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_CheckForDataReady(s_vl53l1x_dev_addr, &ready);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    *is_ready = (ready != 0);
    return AICAM_OK;
}

int vl53l1x_get_distance(uint16_t *distance_mm)
{
    VL53L1X_ERROR api_status;
    
    if (distance_mm == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetDistance(s_vl53l1x_dev_addr, distance_mm);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_result(vl53l1x_result_t *result)
{
    VL53L1X_Result_t api_result;
    VL53L1X_ERROR api_status;
    
    if (result == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetResult(s_vl53l1x_dev_addr, &api_result);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    result->status = api_result.Status;
    result->distance_mm = api_result.Distance;
    result->signal_rate = api_result.SigPerSPAD;
    result->ambient_rate = api_result.Ambient;
    result->spad_count = api_result.NumSPADs;
    result->signal_per_spad = api_result.SigPerSPAD;
    
    return AICAM_OK;
}

int vl53l1x_clear_interrupt(void)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_ClearInterrupt(s_vl53l1x_dev_addr);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_set_distance_mode(vl53l1x_distance_mode_t mode)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (mode != VL53L1X_DISTANCE_MODE_SHORT && mode != VL53L1X_DISTANCE_MODE_LONG) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    api_status = VL53L1X_SetDistanceMode(s_vl53l1x_dev_addr, (uint16_t)mode);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_distance_mode(vl53l1x_distance_mode_t *mode)
{
    uint16_t api_mode;
    VL53L1X_ERROR api_status;
    
    if (mode == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetDistanceMode(s_vl53l1x_dev_addr, &api_mode);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    *mode = (api_mode == 1) ? VL53L1X_DISTANCE_MODE_SHORT : VL53L1X_DISTANCE_MODE_LONG;
    return AICAM_OK;
}

int vl53l1x_set_timing_budget(uint16_t timing_budget_ms)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_SetTimingBudgetInMs(s_vl53l1x_dev_addr, timing_budget_ms);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_timing_budget(uint16_t *timing_budget_ms)
{
    VL53L1X_ERROR api_status;
    
    if (timing_budget_ms == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetTimingBudgetInMs(s_vl53l1x_dev_addr, timing_budget_ms);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_set_intermeasurement_period(uint32_t period_ms)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_SetInterMeasurementInMs(s_vl53l1x_dev_addr, period_ms);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_intermeasurement_period(uint16_t *period_ms)
{
    VL53L1X_ERROR api_status;
    
    if (period_ms == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetInterMeasurementInMs(s_vl53l1x_dev_addr, period_ms);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_set_interrupt_polarity(bool active_high)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_SetInterruptPolarity(s_vl53l1x_dev_addr, active_high ? 1 : 0);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_interrupt_polarity(bool *active_high)
{
    uint8_t polarity;
    VL53L1X_ERROR api_status;
    
    if (active_high == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetInterruptPolarity(s_vl53l1x_dev_addr, &polarity);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    *active_high = (polarity != 0);
    return AICAM_OK;
}

int vl53l1x_set_offset(int16_t offset_mm)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_SetOffset(s_vl53l1x_dev_addr, offset_mm);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_offset(int16_t *offset_mm)
{
    VL53L1X_ERROR api_status;
    
    if (offset_mm == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetOffset(s_vl53l1x_dev_addr, offset_mm);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_set_roi(uint16_t width, uint16_t height)
{
    VL53L1X_ERROR api_status;
    
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    if (width < 4 || height < 4) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    api_status = VL53L1X_SetROI(s_vl53l1x_dev_addr, width, height);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_roi(uint16_t *width, uint16_t *height)
{
    VL53L1X_ERROR api_status;
    
    if (width == NULL || height == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetROI_XY(s_vl53l1x_dev_addr, width, height);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_sensor_id(uint16_t *sensor_id)
{
    VL53L1X_ERROR api_status;
    
    if (sensor_id == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetSensorId(s_vl53l1x_dev_addr, sensor_id);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    return AICAM_OK;
}

int vl53l1x_get_sw_version(uint8_t *major, uint8_t *minor, uint8_t *build, uint32_t *revision)
{
    VL53L1X_Version_t version;
    VL53L1X_ERROR api_status;
    
    if (major == NULL || minor == NULL || build == NULL || revision == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_vl53l1x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    api_status = VL53L1X_GetSWVersion(&version);
    if (api_status != 0) {
        return AICAM_ERROR_HAL_IO;
    }
    
    *major = version.major;
    *minor = version.minor;
    *build = version.build;
    *revision = version.revision;
    
    return AICAM_OK;
}

void vl53l1x_deinit(void)
{
    if (s_vl53l1x_instance == NULL) {
        return;
    }
    
    vl53l1x_platform_set_i2c_instance(NULL);
    i2c_driver_destroy(s_vl53l1x_instance);
    s_vl53l1x_instance = NULL;
    s_vl53l1x_dev_addr = 0;
    /* I2C port deinit is caller's responsibility. */
}
