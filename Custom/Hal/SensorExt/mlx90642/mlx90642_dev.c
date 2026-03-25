/**
 * @file mlx90642_dev.c
 * @brief MLX90642 32×24 IR thermopile array sensor driver implementation.
 *
 * Bridges the Melexis MLX90642 library to the project's i2c_driver layer,
 * following the same pattern used by vl53l1x and sht3x drivers.
 */

#include <stddef.h>
#include "mlx90642_dev.h"
#include "MLX90642_depends.h"
#include "MLX90642.h"
#include "../i2c_driver/i2c_driver.h"
#include "aicam_types.h"
#include "debug.h"
#include "cmsis_os2.h"

/* Declared in MLX90642_depends.c */
extern void mlx90642_depends_set_i2c_instance(i2c_instance_t *instance);

static i2c_instance_t *s_mlx90642_instance = NULL;
static uint8_t         s_mlx90642_addr     = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/** Map MLX90642 library error codes to aicam_result_t. */
static int mlx_err_to_aicam(int mlx_err)
{
    if (mlx_err == 0) {
        return AICAM_OK;
    }
    if (mlx_err == -MLX90642_TIMEOUT_ERR) {
        return AICAM_ERROR_TIMEOUT;
    }
    if (mlx_err == -MLX90642_INVAL_VAL_ERR) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    /* MLX90642_NACK_ERR and any other negative */
    return AICAM_ERROR_HAL_IO;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

int mlx90642_dev_init(uint8_t i2c_addr)
{
    if (i2c_addr == 0U) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance != NULL) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }

    /* i2c_driver expects 8-bit address (7-bit << 1) */
    s_mlx90642_instance = i2c_driver_create(I2C_PORT_1,
                                            (uint16_t)(i2c_addr << 1),
                                            I2C_ADDRESS_7BIT);
    if (s_mlx90642_instance == NULL) {
        LOG_DRV_ERROR("mlx90642: i2c_driver_create failed (ensure I2C port is inited)\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }

    /* Quick presence check */
    int ret = i2c_driver_is_ready(s_mlx90642_instance, 3U, 200U);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("mlx90642: device not responding at address 0x%02X\r\n", i2c_addr);
        i2c_driver_destroy(s_mlx90642_instance);
        s_mlx90642_instance = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }

    /* Provide the i2c instance to the platform layer */
    mlx90642_depends_set_i2c_instance(s_mlx90642_instance);
    s_mlx90642_addr = i2c_addr;

    /* Library init: clears data-ready, triggers sync meas, waits for data */
    int mlx_ret = MLX90642_Init(s_mlx90642_addr);
    if (mlx_ret < 0) {
        LOG_DRV_ERROR("mlx90642: MLX90642_Init failed (%d)\r\n", mlx_ret);
        mlx90642_depends_set_i2c_instance(NULL);
        i2c_driver_destroy(s_mlx90642_instance);
        s_mlx90642_instance = NULL;
        s_mlx90642_addr = 0;
        return mlx_err_to_aicam(mlx_ret);
    }

    /* Read back config for diagnostics */
    {
        int16_t emissivity = 0x4000;
        MLX90642_GetEmissivity(s_mlx90642_addr, &emissivity);

        int16_t tr = 0;
        mlx90642_dev_get_treflected(&tr);

        int rate = MLX90642_GetRefreshRate(s_mlx90642_addr);

        if ((uint16_t)tr == 0x8000U) {
            LOG_DRV_INFO("mlx90642: init OK (addr=0x%02X)  rate=%dHz  emissivity=0x%04X  Treflected=Tsensor-9 (default)\r\n",
                         i2c_addr,
                         (rate >= 2 && rate <= 6) ? (2 << (rate - 2)) : 0,
                         (uint16_t)emissivity);
        } else {
            LOG_DRV_INFO("mlx90642: init OK (addr=0x%02X)  rate=%dHz  emissivity=0x%04X  Treflected=%d (%d.%02d C)\r\n",
                         i2c_addr,
                         (rate >= 2 && rate <= 6) ? (2 << (rate - 2)) : 0,
                         (uint16_t)emissivity,
                         (int)tr,
                         (int)(tr / 100), (int)((tr < 0 ? -tr : tr) % 100));
        }
    }
    return AICAM_OK;
}

void mlx90642_dev_deinit(void)
{
    if (s_mlx90642_instance == NULL) {
        return;
    }
    mlx90642_depends_set_i2c_instance(NULL);
    i2c_driver_destroy(s_mlx90642_instance);
    s_mlx90642_instance = NULL;
    s_mlx90642_addr = 0;
}

/* ------------------------------------------------------------------ */
/* Measurement                                                          */
/* ------------------------------------------------------------------ */

int mlx90642_dev_measure_now(int16_t *pixels)
{
    if (pixels == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_MeasureNow(s_mlx90642_addr, pixels);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_image(int16_t *pixels)
{
    if (pixels == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetImage(s_mlx90642_addr, pixels);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_frame(mlx90642_dev_frame_t *frame)
{
    if (frame == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetFrameData(s_mlx90642_addr,
                                    frame->aux,
                                    frame->raw,
                                    frame->pixels);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_start_sync(void)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_StartSync(s_mlx90642_addr);
    return mlx_err_to_aicam(ret);
}

/* ------------------------------------------------------------------ */
/* Status / flags                                                       */
/* ------------------------------------------------------------------ */

int mlx90642_dev_clear_data_ready(bool *is_ready)
{
    if (is_ready == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_ClearDataReady(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    /* ClearDataReady returns the data-ready state after clearing */
    *is_ready = (ret == MLX90642_YES);
    return AICAM_OK;
}

int mlx90642_dev_is_data_ready(bool *is_ready)
{
    if (is_ready == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_IsDataReady(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *is_ready = (ret == MLX90642_YES);
    return AICAM_OK;
}

int mlx90642_dev_is_read_window_open(bool *is_open)
{
    if (is_open == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_IsReadWindowOpen(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *is_open = (ret == MLX90642_YES);
    return AICAM_OK;
}

int mlx90642_dev_is_busy(bool *is_busy)
{
    if (is_busy == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_IsDeviceBusy(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *is_busy = (ret != MLX90642_NO);
    return AICAM_OK;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

int mlx90642_dev_set_meas_mode(mlx90642_dev_meas_mode_t mode)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_SetMeasMode(s_mlx90642_addr, (uint16_t)mode);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_meas_mode(mlx90642_dev_meas_mode_t *mode)
{
    if (mode == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetMeasMode(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *mode = (mlx90642_dev_meas_mode_t)ret;
    return AICAM_OK;
}

int mlx90642_dev_set_output_format(mlx90642_dev_output_fmt_t fmt)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_SetOutputFormat(s_mlx90642_addr, (uint16_t)fmt);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_output_format(mlx90642_dev_output_fmt_t *fmt)
{
    if (fmt == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetOutputFormat(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *fmt = (mlx90642_dev_output_fmt_t)ret;
    return AICAM_OK;
}

int mlx90642_dev_set_refresh_rate(mlx90642_dev_rate_t rate)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_SetRefreshRate(s_mlx90642_addr, (uint16_t)rate);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_refresh_rate(mlx90642_dev_rate_t *rate)
{
    if (rate == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetRefreshRate(s_mlx90642_addr);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *rate = (mlx90642_dev_rate_t)ret;
    return AICAM_OK;
}

int mlx90642_dev_set_emissivity(int16_t emissivity)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_SetEmissivity(s_mlx90642_addr, emissivity);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_emissivity(int16_t *emissivity)
{
    if (emissivity == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetEmissivity(s_mlx90642_addr, emissivity);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_set_treflected(int16_t tr_centi_celsius)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_SetTreflected(s_mlx90642_addr, tr_centi_celsius);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_treflected(int16_t *tr_centi_celsius)
{
    if (tr_centi_celsius == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    /* Per datasheet Table 20 / section 4.5:
     * Read-back of the active background temperature is at RAM address 0x2E1C.
     * 0x8000 means disabled (device uses Tsensor-9°C).
     * 0xEEEE is the CONFIG write address, not a readable RAM location. */
    uint16_t raw = 0;
    int ret = MLX90642_I2CRead(s_mlx90642_addr, 0x2E1C, 1, &raw);
    if (ret < 0) {
        return mlx_err_to_aicam(ret);
    }
    *tr_centi_celsius = (int16_t)raw;
    return AICAM_OK;
}

/* ------------------------------------------------------------------ */
/* Device information                                                   */
/* ------------------------------------------------------------------ */

int mlx90642_dev_get_id(uint16_t *id)
{
    if (id == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetID(s_mlx90642_addr, id);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_get_fw_version(mlx90642_dev_fw_ver_t *ver)
{
    if (ver == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GetFWver(s_mlx90642_addr,
                                &ver->major, &ver->minor, &ver->patch);
    return mlx_err_to_aicam(ret);
}

/* ------------------------------------------------------------------ */
/* Power management                                                     */
/* ------------------------------------------------------------------ */

int mlx90642_dev_sleep(void)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_GotoSleep(s_mlx90642_addr);
    return mlx_err_to_aicam(ret);
}

int mlx90642_dev_wakeup(void)
{
    if (s_mlx90642_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    int ret = MLX90642_WakeUp(s_mlx90642_addr);
    return mlx_err_to_aicam(ret);
}
