/**
 * @file dts6012m.c
 * @brief DTS6012M long-range TOF (LIDAR) sensor driver implementation.
 *
 * Registers (8-bit address, 8-bit data):
 *   0x00 — distance MSB   (raw mm, big-endian)
 *   0x01 — distance LSB
 *   0x02 — laser enable   (1 = on, 0 = off)
 */

#include "dts6012m.h"
#include "../i2c_driver/i2c_driver.h"
#include "aicam_types.h"
#include "debug.h"

#define DTS6012M_I2C_TIMEOUT_MS     200U

static i2c_instance_t *s_dts6012m_instance = NULL;

/* ------------------------------------------------------------------ */
/* Internal register helpers                                           */
/* ------------------------------------------------------------------ */

static int reg_write(uint8_t reg, uint8_t val)
{
    int ret = i2c_driver_write_reg8(s_dts6012m_instance, reg, &val, 1U,
                                    DTS6012M_I2C_TIMEOUT_MS);
    return (ret > 0) ? AICAM_OK : AICAM_ERROR_HAL_IO;
}

static int reg_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    int ret = i2c_driver_read_reg8(s_dts6012m_instance, reg, buf, len,
                                   DTS6012M_I2C_TIMEOUT_MS);
    return (ret > 0) ? AICAM_OK : AICAM_ERROR_HAL_IO;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int dts6012m_init(uint8_t i2c_addr)
{
    if (i2c_addr == 0U) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_dts6012m_instance != NULL) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }

    /* i2c_driver expects 8-bit address (7-bit << 1) */
    s_dts6012m_instance = i2c_driver_create(I2C_PORT_1,
                                            (uint16_t)(i2c_addr << 1),
                                            I2C_ADDRESS_7BIT);
    if (s_dts6012m_instance == NULL) {
        LOG_DRV_ERROR("dts6012m: i2c_driver_create failed (ensure I2C port is inited)\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }

    int ret = i2c_driver_is_ready(s_dts6012m_instance, 3U, 200U);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("dts6012m: device not responding at address 0x%02X\r\n", i2c_addr);
        i2c_driver_destroy(s_dts6012m_instance);
        s_dts6012m_instance = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }

    /* Turn on laser immediately after init */
    ret = dts6012m_start_laser();
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("dts6012m: start_laser failed %d\r\n", ret);
        i2c_driver_destroy(s_dts6012m_instance);
        s_dts6012m_instance = NULL;
        return ret;
    }

    LOG_DRV_INFO("dts6012m: init OK (addr=0x%02X)\r\n", i2c_addr);
    return AICAM_OK;
}

void dts6012m_deinit(void)
{
    if (s_dts6012m_instance == NULL) {
        return;
    }
    (void)dts6012m_stop_laser();
    i2c_driver_destroy(s_dts6012m_instance);
    s_dts6012m_instance = NULL;
}

/* ------------------------------------------------------------------ */
/* Laser control                                                       */
/* ------------------------------------------------------------------ */

int dts6012m_start_laser(void)
{
    if (s_dts6012m_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return reg_write(DTS6012M_REG_LASER_EN, 1U);
}

int dts6012m_stop_laser(void)
{
    if (s_dts6012m_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return reg_write(DTS6012M_REG_LASER_EN, 0U);
}

/* ------------------------------------------------------------------ */
/* Measurement                                                         */
/* ------------------------------------------------------------------ */

int dts6012m_get_distance_mm(uint16_t *distance_mm)
{
    if (distance_mm == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_dts6012m_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    /* Read MSB and LSB in a single 2-byte burst starting at reg 0x00 */
    uint8_t buf[2];
    int ret = reg_read(DTS6012M_REG_DIST_MSB, buf, 2U);
    if (ret != AICAM_OK) {
        return ret;
    }

    *distance_mm = ((uint16_t)buf[0] << 8) | buf[1];
    return AICAM_OK;
}

int dts6012m_get_distance_m(float *distance_m)
{
    if (distance_m == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }

    uint16_t mm;
    int ret = dts6012m_get_distance_mm(&mm);
    if (ret != AICAM_OK) {
        return ret;
    }

    *distance_m = (float)mm * 0.001f;
    return AICAM_OK;
}
