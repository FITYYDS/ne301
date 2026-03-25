/**
 * @file mlx90642_dev.h
 * @brief MLX90642 32×24 IR thermopile array sensor driver.
 *        Device on I2C port 1, default 7-bit address 0x66.
 *
 * Wraps the Melexis MLX90642 library with the project's i2c_driver layer,
 * following the same pattern used by vl53l1x and sht3x drivers.
 */

#ifndef MLX90642_DEV_H
#define MLX90642_DEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** Default 7-bit I2C slave address */
#define MLX90642_DEV_I2C_ADDR_DEFAULT   (0x66U)

/** Pixel array dimensions */
#define MLX90642_DEV_COLS               (32U)
#define MLX90642_DEV_ROWS               (24U)
#define MLX90642_DEV_PIXEL_COUNT        (MLX90642_DEV_COLS * MLX90642_DEV_ROWS)  /* 768 */

/** Auxiliary data word count */
#define MLX90642_DEV_AUX_COUNT          (20U)

/**
 * @brief Refresh rate setting values (pass to mlx90642_dev_set_refresh_rate).
 *        These match the MLX90642 library constants (MLX90642_REF_RATE_*).
 */
typedef enum {
    MLX90642_DEV_RATE_2HZ  = 2,
    MLX90642_DEV_RATE_4HZ  = 3,
    MLX90642_DEV_RATE_8HZ  = 4,
    MLX90642_DEV_RATE_16HZ = 5,
    MLX90642_DEV_RATE_32HZ = 6,
} mlx90642_dev_rate_t;

/**
 * @brief Measurement mode.
 */
typedef enum {
    MLX90642_DEV_MODE_CONTINUOUS = 0,       /**< Sensor triggers measurements autonomously */
    MLX90642_DEV_MODE_STEP       = 0x0800,  /**< Host triggers each measurement via StartSync */
} mlx90642_dev_meas_mode_t;

/**
 * @brief Output format.
 */
typedef enum {
    MLX90642_DEV_OUTPUT_TEMPERATURE = 0,        /**< Pixel values are temperatures in TO_DATA LSB (0.02°C/LSB; divide by 50 to get °C) */
    MLX90642_DEV_OUTPUT_NORMALIZED  = 0x0100,   /**< Pixel values are normalised IR data */
} mlx90642_dev_output_fmt_t;

/**
 * @brief Firmware version.
 */
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} mlx90642_dev_fw_ver_t;

/**
 * @brief Full frame data (auxiliary + raw IR + processed temperatures).
 */
typedef struct {
    uint16_t aux[MLX90642_DEV_AUX_COUNT];           /**< Auxiliary data words */
    uint16_t raw[MLX90642_DEV_PIXEL_COUNT];          /**< Raw IR pixel words */
    int16_t  pixels[MLX90642_DEV_PIXEL_COUNT + 1];  /**< Processed pixel temps in TO_DATA LSB (0.02°C/LSB); +1 for TA */
} mlx90642_dev_frame_t;

/**
 * @brief Initialize the MLX90642 sensor on I2C port 1.
 *        The caller must have called i2c_driver_init(I2C_PORT_1) beforehand.
 *
 * @param i2c_addr  7-bit I2C address (MLX90642_DEV_I2C_ADDR_DEFAULT = 0x66).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_init(uint8_t i2c_addr);

/**
 * @brief De-initialize and release the I2C instance.
 *        The caller is responsible for i2c_driver_deinit() if needed.
 */
void mlx90642_dev_deinit(void);

/**
 * @brief Read the 768-pixel temperature image in one synchronised measurement.
 *        Clears the data-ready flag, triggers a sync measurement, waits for
 *        completion and copies the result into @p pixels.
 *
 * @param pixels  Output buffer for 768 int16_t pixel values in TO_DATA LSB.
 *                Unit: 0.02°C per LSB  →  temperature_celsius = pixels[i] / 50.0f
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_measure_now(int16_t *pixels);

/**
 * @brief Read the last completed image without triggering a new measurement.
 *        Use after mlx90642_dev_wait_data_ready() returns true.
 *
 * @param pixels  Output buffer for 768 int16_t pixel values in TO_DATA LSB.
 *                Unit: 0.02°C per LSB  →  temperature_celsius = pixels[i] / 50.0f
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_image(int16_t *pixels);

/**
 * @brief Read full frame data: auxiliary words, raw IR pixels, processed temps.
 *
 * @param frame  Pointer to caller-allocated mlx90642_dev_frame_t structure.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_frame(mlx90642_dev_frame_t *frame);

/**
 * @brief Trigger a single synchronised measurement (step mode).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_start_sync(void);

/**
 * @brief Clear the data-ready flag.
 *
 * @param is_ready  Output: true if data was ready when cleared.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_clear_data_ready(bool *is_ready);

/**
 * @brief Poll whether new measurement data is available.
 *
 * @param is_ready  Output: true if new data is ready.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_is_data_ready(bool *is_ready);

/**
 * @brief Poll whether the read window is open (data ready AND device not busy).
 *
 * @param is_open  Output: true if the read window is open.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_is_read_window_open(bool *is_open);

/**
 * @brief Get device busy status.
 *
 * @param is_busy  Output: true if the device is currently busy.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_is_busy(bool *is_busy);

/**
 * @brief Set measurement mode (continuous or step).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_set_meas_mode(mlx90642_dev_meas_mode_t mode);

/**
 * @brief Get current measurement mode.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_meas_mode(mlx90642_dev_meas_mode_t *mode);

/**
 * @brief Set pixel output format (temperature or normalised data).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_set_output_format(mlx90642_dev_output_fmt_t fmt);

/**
 * @brief Get current pixel output format.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_output_format(mlx90642_dev_output_fmt_t *fmt);

/**
 * @brief Set pixel refresh rate.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_set_refresh_rate(mlx90642_dev_rate_t rate);

/**
 * @brief Get current pixel refresh rate.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_refresh_rate(mlx90642_dev_rate_t *rate);

/**
 * @brief Set emissivity (scaled: 0x4000 = 1.0).
 * @param emissivity  Fixed-point emissivity value (1..0x4000).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_set_emissivity(int16_t emissivity);

/**
 * @brief Get current emissivity setting.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_emissivity(int16_t *emissivity);

/**
 * @brief Set the reflected (ambient) temperature for compensation (centi-°C).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_set_treflected(int16_t tr_centi_celsius);

/**
 * @brief Get the current reflected temperature from EEPROM (centi-°C).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_treflected(int16_t *tr_centi_celsius);

/**
 * @brief Read device unique ID (4 × 16-bit words).
 * @param id  Output buffer of at least 4 uint16_t elements.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_id(uint16_t *id);

/**
 * @brief Read firmware version from the sensor.
 * @param ver  Output firmware version structure.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_get_fw_version(mlx90642_dev_fw_ver_t *ver);

/**
 * @brief Send the sensor to sleep (low-power state).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_sleep(void);

/**
 * @brief Wake the sensor from sleep.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int mlx90642_dev_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif /* MLX90642_DEV_H */
