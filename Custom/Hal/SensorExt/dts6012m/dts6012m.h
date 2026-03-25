/**
 * @file dts6012m.h
 * @brief DTS6012M long-range TOF (LIDAR) sensor driver.
 *        Device on I2C port 1, default 7-bit address 0x51 (programmable).
 *
 * Register map:
 *   0x00 — distance MSB (raw unit: mm)
 *   0x01 — distance LSB
 *   0x02 — laser enable  (1 = on / measuring, 0 = off)
 */

#ifndef DTS6012M_H
#define DTS6012M_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** Default 7-bit I2C slave address (programmable via sensor config) */
#define DTS6012M_I2C_ADDR_DEFAULT   (0x51U)

/** Register addresses */
#define DTS6012M_REG_DIST_MSB       (0x00U)
#define DTS6012M_REG_DIST_LSB       (0x01U)
#define DTS6012M_REG_LASER_EN       (0x02U)

/** Raw distance unit is millimetres. */
#define DTS6012M_RAW_UNIT_MM        (1U)

/**
 * @brief Initialize the DTS6012M sensor on I2C port 1.
 *        Caller must have called i2c_driver_init(I2C_PORT_1) beforehand.
 *        On success the laser is turned on automatically.
 *
 * @param i2c_addr  7-bit I2C address (DTS6012M_I2C_ADDR_DEFAULT = 0x51).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int dts6012m_init(uint8_t i2c_addr);

/**
 * @brief De-initialize the sensor and release the I2C instance.
 *        The laser is turned off before releasing.
 *        Caller is responsible for i2c_driver_deinit() if needed.
 */
void dts6012m_deinit(void);

/**
 * @brief Enable the laser (start measuring).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int dts6012m_start_laser(void);

/**
 * @brief Disable the laser (stop measuring).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int dts6012m_stop_laser(void);

/**
 * @brief Read the current distance measurement.
 *
 * @param distance_mm  Output: distance in millimetres.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int dts6012m_get_distance_mm(uint16_t *distance_mm);

/**
 * @brief Read the current distance measurement in metres.
 *
 * @param distance_m  Output: distance in metres.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int dts6012m_get_distance_m(float *distance_m);

#ifdef __cplusplus
}
#endif

#endif /* DTS6012M_H */
