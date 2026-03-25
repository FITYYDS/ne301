/**
 * @file sht3x.h
 * @brief Temperature and humidity sensor driver (I2C) for SHT3x series (SHT30/SHT31/SHT35).
 *        Device on I2C port 1, 7-bit address 0x44 (default) or 0x45 (ADDR pin high).
 */

#ifndef SHT3X_H
#define SHT3X_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** I2C 7-bit address (default, ADDR pin grounded) */
#define SHT3X_I2C_ADDR_DEFAULT          (0x44U)
/** I2C 7-bit address (alternative, ADDR pin high) */
#define SHT3X_I2C_ADDR_ALT              (0x45U)

/** Measurement commands - Single shot, high repeatability */
#define SHT3X_CMD_MEASURE_SINGLE_H      (0x2400U)  /* Clock stretching enabled */
#define SHT3X_CMD_MEASURE_SINGLE_M      (0x240BU)  /* Medium repeatability */
#define SHT3X_CMD_MEASURE_SINGLE_L      (0x2416U)  /* Low repeatability */

/** Measurement commands - Single shot, high repeatability, no clock stretching */
#define SHT3X_CMD_MEASURE_SINGLE_H_NCS  (0x2402U)
#define SHT3X_CMD_MEASURE_SINGLE_M_NCS  (0x240DU)
#define SHT3X_CMD_MEASURE_SINGLE_L_NCS  (0x2418U)

/** Periodic measurement commands - 0.5 mps (measurements per second) */
#define SHT3X_CMD_MEASURE_PERIODIC_05_H (0x2032U)
#define SHT3X_CMD_MEASURE_PERIODIC_05_M (0x2024U)
#define SHT3X_CMD_MEASURE_PERIODIC_05_L (0x202FU)

/** Periodic measurement commands - 1 mps */
#define SHT3X_CMD_MEASURE_PERIODIC_1_H  (0x2130U)
#define SHT3X_CMD_MEASURE_PERIODIC_1_M  (0x2126U)
#define SHT3X_CMD_MEASURE_PERIODIC_1_L  (0x212DU)

/** Periodic measurement commands - 2 mps */
#define SHT3X_CMD_MEASURE_PERIODIC_2_H  (0x2236U)
#define SHT3X_CMD_MEASURE_PERIODIC_2_M  (0x2220U)
#define SHT3X_CMD_MEASURE_PERIODIC_2_L  (0x222BU)

/** Periodic measurement commands - 4 mps */
#define SHT3X_CMD_MEASURE_PERIODIC_4_H  (0x2334U)
#define SHT3X_CMD_MEASURE_PERIODIC_4_M  (0x2322U)
#define SHT3X_CMD_MEASURE_PERIODIC_4_L  (0x2329U)

/** Periodic measurement commands - 10 mps */
#define SHT3X_CMD_MEASURE_PERIODIC_10_H (0x2737U)
#define SHT3X_CMD_MEASURE_PERIODIC_10_M (0x2721U)
#define SHT3X_CMD_MEASURE_PERIODIC_10_L (0x272AU)

/** Fetch data command (for periodic mode) */
#define SHT3X_CMD_FETCH_DATA            (0xE000U)

/** Break command (stop periodic measurement) */
#define SHT3X_CMD_BREAK                 (0x3093U)

/** Soft reset command */
#define SHT3X_CMD_SOFT_RESET            (0x30A2U)

/** Heater enable command */
#define SHT3X_CMD_HEATER_ENABLE         (0x306DU)

/** Heater disable command */
#define SHT3X_CMD_HEATER_DISABLE        (0x3066U)

/** Status register read command */
#define SHT3X_CMD_READ_STATUS           (0xF32DU)

/** Clear status register command */
#define SHT3X_CMD_CLEAR_STATUS          (0x3041U)

/** Measurement delay times (ms) */
#define SHT3X_DELAY_HIGH_REP             (15U)   /* High repeatability */
#define SHT3X_DELAY_MEDIUM_REP           (6U)    /* Medium repeatability */
#define SHT3X_DELAY_LOW_REP              (4U)    /* Low repeatability */

/** Reset delay (ms) */
#define SHT3X_RESET_DELAY_MS            (1U)

/**
 * @brief Initialize the SHT3x sensor on I2C port 1.
 *        Caller must have initialized the I2C port (e.g. i2c_driver_init(I2C_PORT_1)) first.
 * @param i2c_addr I2C 7-bit address (SHT3X_I2C_ADDR_DEFAULT or SHT3X_I2C_ADDR_ALT).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_init(uint8_t i2c_addr);

/**
 * @brief Read current temperature and humidity measurement (single shot, high repeatability).
 * @param temperature Output: Temperature in degrees Celsius.
 * @param humidity Output: Relative humidity in percent (0-100%).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_read_measurement(float *temperature, float *humidity);

/**
 * @brief Read measurement with specified command (allows choosing repeatability).
 * @param cmd Measurement command (e.g., SHT3X_CMD_MEASURE_SINGLE_H).
 * @param temperature Output: Temperature in degrees Celsius.
 * @param humidity Output: Relative humidity in percent (0-100%).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_read_measurement_cmd(uint16_t cmd, float *temperature, float *humidity);

/**
 * @brief Start periodic measurement mode.
 * @param cmd Periodic measurement command (e.g., SHT3X_CMD_MEASURE_PERIODIC_1_H).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_start_periodic(uint16_t cmd);

/**
 * @brief Fetch data from periodic measurement mode.
 * @param temperature Output: Temperature in degrees Celsius.
 * @param humidity Output: Relative humidity in percent (0-100%).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_fetch_data(float *temperature, float *humidity);

/**
 * @brief Stop periodic measurement mode.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_stop_periodic(void);

/**
 * @brief Software reset the sensor.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_reset(void);

/**
 * @brief Enable or disable the heater.
 * @param enable true to enable heater, false to disable.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_set_heater(bool enable);

/**
 * @brief Read status register.
 * @param status Output: Status register value (16-bit).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_read_status(uint16_t *status);

/**
 * @brief Clear status register.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int sht3x_clear_status(void);

/**
 * @brief De-initialize the sensor and release the I2C instance only.
 *        Caller is responsible for I2C port deinit (e.g. i2c_driver_deinit(I2C_PORT_1)) if needed.
 */
void sht3x_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_H */
