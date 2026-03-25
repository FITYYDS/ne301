/**
 * @file vl53l1x.h
 * @brief VL53L1X ToF (Time-of-Flight) ranging sensor driver (I2C).
 *        Device on I2C port 1, 7-bit address 0x52 (default).
 */

#ifndef VL53L1X_H
#define VL53L1X_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** I2C 7-bit address (default) */
#define VL53L1X_I2C_ADDR_DEFAULT          (0x29U)  /* 7-bit address, 8-bit write addr is 0x52 */

/** Distance mode */
typedef enum {
    VL53L1X_DISTANCE_MODE_SHORT = 1,  /* Short mode: max 1.3m, better ambient immunity */
    VL53L1X_DISTANCE_MODE_LONG = 2,   /* Long mode: max 4m in dark with 200ms timing budget */
} vl53l1x_distance_mode_t;

/** Timing budget values (ms) */
#define VL53L1X_TIMING_BUDGET_15MS        (15U)
#define VL53L1X_TIMING_BUDGET_20MS        (20U)
#define VL53L1X_TIMING_BUDGET_33MS        (33U)
#define VL53L1X_TIMING_BUDGET_50MS        (50U)
#define VL53L1X_TIMING_BUDGET_100MS       (100U)  /* Default */
#define VL53L1X_TIMING_BUDGET_200MS       (200U)
#define VL53L1X_TIMING_BUDGET_500MS       (500U)

/** Range status values */
#define VL53L1X_RANGE_STATUS_OK           (0U)    /* No error */
#define VL53L1X_RANGE_STATUS_SIGMA_FAIL   (1U)    /* Sigma failed */
#define VL53L1X_RANGE_STATUS_SIGNAL_FAIL  (2U)    /* Signal failed */
#define VL53L1X_RANGE_STATUS_MIN_RANGE    (3U)    /* Below minimum range */
#define VL53L1X_RANGE_STATUS_PHASE_FAIL   (4U)    /* Phase failed */
#define VL53L1X_RANGE_STATUS_HW_FAIL      (5U)    /* Hardware failure */
#define VL53L1X_RANGE_STATUS_WRAP_AROUND  (7U)    /* Wrap-around */

/** Ranging result structure */
typedef struct {
    uint8_t status;         /* Range status (0=OK, see VL53L1X_RANGE_STATUS_*) */
    uint16_t distance_mm;   /* Distance in millimeters */
    uint16_t signal_rate;   /* Signal rate in kcps (kilo counts per second) */
    uint16_t ambient_rate;  /* Ambient rate in kcps */
    uint16_t spad_count;    /* Number of enabled SPADs */
    uint16_t signal_per_spad; /* Signal per SPAD in kcps/SPAD */
} vl53l1x_result_t;

/**
 * @brief Initialize the VL53L1X sensor on I2C port 1.
 *        Caller must have initialized the I2C port (e.g. i2c_driver_init(I2C_PORT_1)) first.
 * @param i2c_addr I2C 7-bit address (default VL53L1X_I2C_ADDR_DEFAULT = 0x52).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_init(uint8_t i2c_addr);

/**
 * @brief Start ranging operation.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_start_ranging(void);

/**
 * @brief Stop ranging operation.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_stop_ranging(void);

/**
 * @brief Check if new ranging data is ready.
 * @param is_ready Output: true if data is ready, false otherwise.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_check_data_ready(bool *is_ready);

/**
 * @brief Get distance measurement in millimeters.
 * @param distance_mm Output: Distance in millimeters.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_distance(uint16_t *distance_mm);

/**
 * @brief Get complete ranging result.
 * @param result Output: Ranging result structure.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_result(vl53l1x_result_t *result);

/**
 * @brief Clear interrupt (call after reading data to arm next interrupt).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_clear_interrupt(void);

/**
 * @brief Set distance mode.
 * @param mode Distance mode (VL53L1X_DISTANCE_MODE_SHORT or VL53L1X_DISTANCE_MODE_LONG).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_set_distance_mode(vl53l1x_distance_mode_t mode);

/**
 * @brief Get distance mode.
 * @param mode Output: Distance mode.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_distance_mode(vl53l1x_distance_mode_t *mode);

/**
 * @brief Set timing budget in milliseconds.
 * @param timing_budget_ms Timing budget (15, 20, 33, 50, 100, 200, or 500 ms).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_set_timing_budget(uint16_t timing_budget_ms);

/**
 * @brief Get timing budget in milliseconds.
 * @param timing_budget_ms Output: Timing budget in milliseconds.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_timing_budget(uint16_t *timing_budget_ms);

/**
 * @brief Set inter-measurement period in milliseconds.
 *        Must be >= timing budget.
 * @param period_ms Inter-measurement period in milliseconds.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_set_intermeasurement_period(uint32_t period_ms);

/**
 * @brief Get inter-measurement period in milliseconds.
 * @param period_ms Output: Inter-measurement period in milliseconds.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_intermeasurement_period(uint16_t *period_ms);

/**
 * @brief Set interrupt polarity.
 * @param active_high true for active high (default), false for active low.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_set_interrupt_polarity(bool active_high);

/**
 * @brief Get interrupt polarity.
 * @param active_high Output: true if active high, false if active low.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_interrupt_polarity(bool *active_high);

/**
 * @brief Set offset correction in millimeters.
 * @param offset_mm Offset correction value in millimeters.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_set_offset(int16_t offset_mm);

/**
 * @brief Get offset correction in millimeters.
 * @param offset_mm Output: Offset correction value in millimeters.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_offset(int16_t *offset_mm);

/**
 * @brief Set ROI (Region of Interest) size.
 * @param width ROI width (minimum 4).
 * @param height ROI height (minimum 4).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_set_roi(uint16_t width, uint16_t height);

/**
 * @brief Get ROI size.
 * @param width Output: ROI width.
 * @param height Output: ROI height.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_roi(uint16_t *width, uint16_t *height);

/**
 * @brief Get sensor ID (should be 0xEEAC).
 * @param sensor_id Output: Sensor ID.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_sensor_id(uint16_t *sensor_id);

/**
 * @brief Get software version.
 * @param major Output: Major version number.
 * @param minor Output: Minor version number.
 * @param build Output: Build number.
 * @param revision Output: Revision number.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int vl53l1x_get_sw_version(uint8_t *major, uint8_t *minor, uint8_t *build, uint32_t *revision);

/**
 * @brief De-initialize the sensor and release the I2C instance only.
 *        Caller is responsible for I2C port deinit (e.g. i2c_driver_deinit(I2C_PORT_1)) if needed.
 */
void vl53l1x_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* VL53L1X_H */
