/**
 * @file ltr_31x.h
 * @brief Ambient light sensor driver (I2C) for LTR-310 / LTR-311ALS-02.
 *        Register map follows LTR-311; compatible with LTR-310 (same I2C interface).
 *        Device on I2C port 1, 7-bit address 0x22.
 */

#ifndef LTR_31X_H
#define LTR_31X_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** I2C 7-bit address */
#define LTR_31X_I2C_ADDR          (0x22U)

/** Register addresses (LTR-311 map; LTR-310 compatible) */
#define LTR_31X_REG_ALS_AVERAGING       (0x7FU)
#define LTR_31X_REG_ALS_CONTR           (0x80U)
#define LTR_31X_REG_RESET              (0x81U)
#define LTR_31X_REG_ALS_TIME_SCALE     (0x85U)
#define LTR_31X_REG_ALS_INT_TIME_STEPS (0x86U)
#define LTR_31X_REG_ALS_MRR_STEPS      (0x87U)
#define LTR_31X_REG_ALS_STATUS         (0x88U)
#define LTR_31X_REG_ALS_DATA_LSB       (0x8BU)
#define LTR_31X_REG_ALS_DATA_MSB       (0x8CU)
#define LTR_31X_REG_ALS_IR_DATA_LSB    (0x8DU)
#define LTR_31X_REG_ALS_IR_DATA_MSB    (0x8EU)
#define LTR_31X_REG_IR_ENABLE          (0x95U)
#define LTR_31X_REG_INTERRUPT          (0xA0U)
#define LTR_31X_REG_INTERRUPT_PERSIST  (0xA1U)
#define LTR_31X_REG_ALS_THRES_HIGH_LSB (0xAAU)
#define LTR_31X_REG_ALS_THRES_HIGH_MSB (0xABU)
#define LTR_31X_REG_ALS_THRES_LOW_LSB  (0xACU)
#define LTR_31X_REG_ALS_THRES_LOW_MSB  (0xADU)
#define LTR_31X_REG_PART_ID            (0xAEU)
#define LTR_31X_REG_MANUFAC_ID         (0xAFU)

/** Expected ID values for verification (LTR-310 / LTR-311) */
#define LTR_31X_PART_ID_VAL            (0x90U)
#define LTR_31X_MANUFAC_ID_VAL         (0x05U)  /* LTR-311 typical */
#define LTR_31X_MANUFAC_ID_VAL_ALT     (0x06U)  /* LTR-310 / some parts */

/** ALS_CONTR: bit to enable ALS active mode (typical) */
#define LTR_31X_ALS_CONTR_ACTIVE       (0x01U)

/** RESET: write 0xA5 to trigger software reset (datasheet typical) */
#define LTR_31X_RESET_MAGIC            (0xA5U)

/**
 * @brief Initialize the ALS sensor (LTR-310 / LTR-311ALS-02) on I2C port 1.
 *        Caller must have initialized the I2C port (e.g. i2c_driver_init(I2C_PORT_1)) first.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int ltr_31x_init(void);

/**
 * @brief Read current ALS (ambient light) measurement data (16-bit).
 * @param als Output: ALS count value (0..65535).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int ltr_31x_read_als(uint16_t *als);

/**
 * @brief Read current ALS infrared measurement data (16-bit).
 * @param ir Output: IR count value (0..65535).
 * @return 0 on success, negative aicam_result_t on failure.
 */
int ltr_31x_read_als_ir(uint16_t *ir);

/**
 * @brief De-initialize the sensor and release the I2C instance only.
 *        Caller is responsible for I2C port deinit (e.g. i2c_driver_deinit(I2C_PORT_1)) if needed.
 */
void ltr_31x_deinit(void);

/**
 * @brief Software reset the IC (RESET register), then re-enable ALS active mode.
 *        No need to call init() again; read_als/read_als_ir can be used immediately after.
 * @return 0 on success, negative aicam_result_t on failure.
 */
int ltr_31x_reset(void);

/**
 * @brief Set ALS integration time and measurement rate (registers 0x85, 0x86, 0x87).
 * @param time_scale  ALS_TIME_SCALE (0x85), default 0x02.
 * @param int_steps   ALS_INT_TIME_STEPS (0x86), default 0x3F.
 * @param mrr_steps   ALS_MRR_STEPS (0x87), default 0xFF.
 * @return 0 on success, negative on failure.
 */
int ltr_31x_set_timing(uint8_t time_scale, uint8_t int_steps, uint8_t mrr_steps);

/**
 * @brief Set ALS averaging (register 0x7F). Default 0x07.
 * @param averaging Register value (e.g. 0x07).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_set_als_averaging(uint8_t averaging);

/**
 * @brief Set ALS interrupt thresholds (0xAA-0xAD). High/low 16-bit values.
 * @param thresh_high Upper threshold (0xAA LSB, 0xAB MSB).
 * @param thresh_low  Lower threshold (0xAC LSB, 0xAD MSB).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_set_thresholds(uint16_t thresh_high, uint16_t thresh_low);

/**
 * @brief Read ALS status register (0x88).
 * @param status Output: ALS_STATUS value.
 * @return 0 on success, negative on failure.
 */
int ltr_31x_read_status(uint8_t *status);

/**
 * @brief Read PART_ID (0xAE) for verification.
 * @param part_id Output: PART_ID value (expected 0x90).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_get_part_id(uint8_t *part_id);

/**
 * @brief Read MANUFAC_ID (0xAF) for verification.
 * @param manufac_id Output: MANUFAC_ID value (expected 0x05).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_get_manufac_id(uint8_t *manufac_id);

/**
 * @brief Set IR channel enable (register 0x95, bit 3).
 * @param enable true to enable IR channel, false to disable.
 * @return 0 on success, negative on failure.
 */
int ltr_31x_set_ir_enable(bool enable);

/**
 * @brief Read IR_ENABLE register (0x95).
 * @param enabled Output: true if IR channel is enabled, false if disabled.
 * @return 0 on success, negative on failure.
 */
int ltr_31x_get_ir_enable(bool *enabled);

/**
 * @brief Configure interrupt pin behavior (register 0xA0).
 * @param polarity true = active high, false = active low (default).
 * @param mode true = ALS measurement can trigger interrupt, false = INACTIVE/high impedance (default).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_set_interrupt_config(bool polarity, bool mode);

/**
 * @brief Read INTERRUPT register (0xA0).
 * @param polarity Output: true if active high, false if active low.
 * @param mode Output: true if interrupt enabled, false if INACTIVE.
 * @return 0 on success, negative on failure.
 */
int ltr_31x_get_interrupt_config(bool *polarity, bool *mode);

/**
 * @brief Set interrupt persist count (register 0xA1, bits 3:0).
 *        Sets how many consecutive ALS values out of threshold range before asserting interrupt.
 * @param persist_count Number of consecutive out-of-range measurements (0-15).
 *                      0 = every ALS value out of threshold range (default).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_set_interrupt_persist(uint8_t persist_count);

/**
 * @brief Read INTERRUPT_PERSIST register (0xA1).
 * @param persist_count Output: persist count value (0-15).
 * @return 0 on success, negative on failure.
 */
int ltr_31x_get_interrupt_persist(uint8_t *persist_count);

#ifdef __cplusplus
}
#endif

#endif /* LTR_31X_H */
