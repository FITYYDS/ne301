/**
 * @file ltr_31x.c
 * @brief Ambient light sensor driver for LTR-310 / LTR-311ALS-02 (I2C port 1, addr 0x22).
 *        Register map from LTR-311; LTR-310 uses the same I2C register interface.
 */

#include <stddef.h>
#include "ltr_31x.h"
#include "aicam_types.h"
#include "debug.h"
#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "../i2c_driver/i2c_driver.h"

#define LTR_31X_I2C_TIMEOUT_MS    (200U)
/** Delay after software reset before re-enabling ALS (datasheet: IC needs time to reset). */
#define LTR_31X_RESET_DELAY_MS    (5U)

static i2c_instance_t *s_ltr_instance = NULL;

int ltr_31x_init(void)
{
    int ret;
    uint8_t id;

    if (s_ltr_instance != NULL) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }

    /* I2C port init/deinit is caller's responsibility; only create instance here. */
    /* 7-bit address 0x22 -> HAL 8-bit address (addr << 1) */
    s_ltr_instance = i2c_driver_create(I2C_PORT_1,
                                       (uint16_t)(LTR_31X_I2C_ADDR << 1),
                                       I2C_ADDRESS_7BIT);
    if (s_ltr_instance == NULL) {
        LOG_DRV_ERROR("ltr_31x: i2c_driver_create failed (ensure I2C port is inited)\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }

    ret = i2c_driver_is_ready(s_ltr_instance, 3, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("ltr_31x: device not ready\r\n");
        i2c_driver_destroy(s_ltr_instance);
        s_ltr_instance = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }

    /* Verify PART_ID */
    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_PART_ID, &id, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0 || id != LTR_31X_PART_ID_VAL) {
        LOG_DRV_ERROR("ltr_31x: PART_ID read failed or mismatch (got 0x%02X)\r\n", id);
        i2c_driver_destroy(s_ltr_instance);
        s_ltr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }

    /* Verify MANUFAC_ID (accept 0x05 or 0x06, part variants) */
    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_MANUFAC_ID, &id, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0 || (id != LTR_31X_MANUFAC_ID_VAL && id != LTR_31X_MANUFAC_ID_VAL_ALT)) {
        LOG_DRV_ERROR("ltr_31x: MANUFAC_ID read failed or mismatch (got 0x%02X)\r\n", id);
        i2c_driver_destroy(s_ltr_instance);
        s_ltr_instance = NULL;
        return AICAM_ERROR_PROTOCOL;
    }

    /* Enable ALS active mode (ALS_CONTR = 0x01) */
    id = LTR_31X_ALS_CONTR_ACTIVE;
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_CONTR, &id, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        LOG_DRV_ERROR("ltr_31x: ALS_CONTR write failed\r\n");
        i2c_driver_destroy(s_ltr_instance);
        s_ltr_instance = NULL;
        return AICAM_ERROR_HAL_IO;
    }

    return AICAM_OK;
}

int ltr_31x_read_als(uint16_t *als)
{
    uint8_t buf[2];
    int ret;

    if (als == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_ALS_DATA_LSB, buf, 2, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 2) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }

    *als = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return AICAM_OK;
}

int ltr_31x_read_als_ir(uint16_t *ir)
{
    uint8_t buf[2];
    int ret;

    if (ir == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_ALS_IR_DATA_LSB, buf, 2, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 2) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }

    *ir = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return AICAM_OK;
}

void ltr_31x_deinit(void)
{
    if (s_ltr_instance == NULL) {
        return;
    }

    i2c_driver_destroy(s_ltr_instance);
    s_ltr_instance = NULL;
    /* I2C port deinit is caller's responsibility. */
}

int ltr_31x_reset(void)
{
    uint8_t val;
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    val = LTR_31X_RESET_MAGIC;
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_RESET, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }

    osDelay(LTR_31X_RESET_DELAY_MS);

    /* Re-enable ALS active mode (reset clears ALS_CONTR to standby). No need to re-init. */
    val = LTR_31X_ALS_CONTR_ACTIVE;
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_CONTR, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    return AICAM_OK;
}

int ltr_31x_set_timing(uint8_t time_scale, uint8_t int_steps, uint8_t mrr_steps)
{
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_TIME_SCALE, &time_scale, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) return (ret < 0) ? ret : AICAM_ERROR_IO;
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_INT_TIME_STEPS, &int_steps, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) return (ret < 0) ? ret : AICAM_ERROR_IO;
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_MRR_STEPS, &mrr_steps, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_set_als_averaging(uint8_t averaging)
{
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_AVERAGING, &averaging, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret <= 0) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_set_thresholds(uint16_t thresh_high, uint16_t thresh_low)
{
    uint8_t buf[2];
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    buf[0] = (uint8_t)(thresh_high & 0xFFU);
    buf[1] = (uint8_t)(thresh_high >> 8);
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_THRES_HIGH_LSB, buf, 2, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 2) return (ret < 0) ? ret : AICAM_ERROR_IO;

    buf[0] = (uint8_t)(thresh_low & 0xFFU);
    buf[1] = (uint8_t)(thresh_low >> 8);
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_ALS_THRES_LOW_LSB, buf, 2, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 2) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_read_status(uint8_t *status)
{
    int ret;

    if (status == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_ALS_STATUS, status, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_get_part_id(uint8_t *part_id)
{
    int ret;

    if (part_id == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_PART_ID, part_id, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_get_manufac_id(uint8_t *manufac_id)
{
    int ret;

    if (manufac_id == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_MANUFAC_ID, manufac_id, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_set_ir_enable(bool enable)
{
    uint8_t val;
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    /* IR_ENABLE (0x95): bit 3 = IR Enable, bits 7:4 and 2:0 must be 0 */
    val = enable ? 0x08U : 0x00U;  /* Bit 3 = enable, others = 0 */
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_IR_ENABLE, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_get_ir_enable(bool *enabled)
{
    uint8_t val;
    int ret;

    if (enabled == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_IR_ENABLE, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    *enabled = ((val & 0x08U) != 0U);  /* Bit 3 */
    return AICAM_OK;
}

int ltr_31x_set_interrupt_config(bool polarity, bool mode)
{
    uint8_t val;
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    /* INTERRUPT (0xA0): bit 3 = polarity, bit 2 = mode, bits 7:4 and 1:0 must be 0 */
    val = 0x00U;
    if (polarity) val |= 0x08U;  /* Bit 3 */
    if (mode) val |= 0x04U;       /* Bit 2 */
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_INTERRUPT, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_get_interrupt_config(bool *polarity, bool *mode)
{
    uint8_t val;
    int ret;

    if (polarity == NULL || mode == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_INTERRUPT, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    *polarity = ((val & 0x08U) != 0U);  /* Bit 3 */
    *mode = ((val & 0x04U) != 0U);       /* Bit 2 */
    return AICAM_OK;
}

int ltr_31x_set_interrupt_persist(uint8_t persist_count)
{
    uint8_t val;
    int ret;

    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    if (persist_count > 15U) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    /* INTERRUPT_PERSIST (0xA1): bits 3:0 = persist count, bits 7:4 must be 0 */
    val = persist_count & 0x0FU;  /* Only bits 3:0 */
    ret = i2c_driver_write_reg8(s_ltr_instance, LTR_31X_REG_INTERRUPT_PERSIST, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    return AICAM_OK;
}

int ltr_31x_get_interrupt_persist(uint8_t *persist_count)
{
    uint8_t val;
    int ret;

    if (persist_count == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_ltr_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    ret = i2c_driver_read_reg8(s_ltr_instance, LTR_31X_REG_INTERRUPT_PERSIST, &val, 1, LTR_31X_I2C_TIMEOUT_MS);
    if (ret != 1) return (ret < 0) ? ret : AICAM_ERROR_IO;
    *persist_count = val & 0x0FU;  /* Bits 3:0 */
    return AICAM_OK;
}
