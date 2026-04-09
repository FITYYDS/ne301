/**
 * @file sht3x.c
 * @brief Temperature and humidity sensor driver for SHT3x series (I2C port 1).
 */

#include <stddef.h>
#include <string.h>
#include "sht3x.h"
#include "aicam_types.h"
#include "debug.h"
#include "stm32n6xx_hal.h"
#include "cmsis_os2.h"
#include "../i2c_driver/i2c_driver.h"

#define SHT3X_I2C_TIMEOUT_MS    (200U)

/* CRC-8 polynomial: 0x31 (x^8 + x^5 + x^4 + 1) */
#define SHT3X_CRC8_POLYNOMIAL   (0x31U)

static i2c_instance_t *s_sht3x_instance = NULL;

/**
 * @brief Calculate CRC-8 checksum for SHT3x data
 * @param data Pointer to data buffer
 * @param len Length of data in bytes
 * @return CRC-8 checksum
 */
static uint8_t sht3x_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFU;
    uint8_t i, j;
    
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1) ^ SHT3X_CRC8_POLYNOMIAL);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/**
 * @brief Get measurement delay based on command
 * @param cmd Measurement command
 * @return Delay in milliseconds
 */
static uint32_t sht3x_get_delay(uint16_t cmd)
{
    /* Single shot high repeatability */
    if (cmd == SHT3X_CMD_MEASURE_SINGLE_H || cmd == SHT3X_CMD_MEASURE_SINGLE_H_NCS) {
        return SHT3X_DELAY_HIGH_REP;
    }
    /* Single shot medium repeatability */
    else if (cmd == SHT3X_CMD_MEASURE_SINGLE_M || cmd == SHT3X_CMD_MEASURE_SINGLE_M_NCS) {
        return SHT3X_DELAY_MEDIUM_REP;
    }
    /* Single shot low repeatability */
    else if (cmd == SHT3X_CMD_MEASURE_SINGLE_L || cmd == SHT3X_CMD_MEASURE_SINGLE_L_NCS) {
        return SHT3X_DELAY_LOW_REP;
    }
    /* Periodic high repeatability (check last nibble: 0x2, 0x6, 0x7) */
    else if (cmd == SHT3X_CMD_MEASURE_PERIODIC_05_H || 
             cmd == SHT3X_CMD_MEASURE_PERIODIC_1_H ||
             cmd == SHT3X_CMD_MEASURE_PERIODIC_2_H ||
             cmd == SHT3X_CMD_MEASURE_PERIODIC_4_H ||
             cmd == SHT3X_CMD_MEASURE_PERIODIC_10_H) {
        return SHT3X_DELAY_HIGH_REP;
    }
    /* Periodic medium repeatability (check last nibble: 0x4, 0x0, 0x2, 0x2, 0x1) */
    else if (cmd == SHT3X_CMD_MEASURE_PERIODIC_05_M || 
             cmd == SHT3X_CMD_MEASURE_PERIODIC_1_M ||
             cmd == SHT3X_CMD_MEASURE_PERIODIC_2_M ||
             cmd == SHT3X_CMD_MEASURE_PERIODIC_4_M ||
             cmd == SHT3X_CMD_MEASURE_PERIODIC_10_M) {
        return SHT3X_DELAY_MEDIUM_REP;
    }
    /* Periodic low repeatability or default */
    else {
        return SHT3X_DELAY_LOW_REP;
    }
}

int sht3x_init(uint8_t i2c_addr)
{
    int ret;
    
    if (i2c_addr != SHT3X_I2C_ADDR_DEFAULT && i2c_addr != SHT3X_I2C_ADDR_ALT) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    if (s_sht3x_instance != NULL) {
        return AICAM_ERROR_ALREADY_INITIALIZED;
    }
    
    /* I2C port init/deinit is caller's responsibility; only create instance here. */
    /* 7-bit address -> HAL 8-bit address (addr << 1) */
    s_sht3x_instance = i2c_driver_create(I2C_PORT_1,
                                         (uint16_t)(i2c_addr << 1),
                                         I2C_ADDRESS_7BIT);
    if (s_sht3x_instance == NULL) {
        LOG_DRV_ERROR("sht3x: i2c_driver_create failed (ensure I2C port is inited)\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    ret = i2c_driver_is_ready(s_sht3x_instance, 3, SHT3X_I2C_TIMEOUT_MS);
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("sht3x: device not ready\r\n");
        i2c_driver_destroy(s_sht3x_instance);
        s_sht3x_instance = NULL;
        return AICAM_ERROR_NOT_FOUND;
    }
    
    /* Soft reset to ensure clean state */
    ret = sht3x_reset();
    if (ret != AICAM_OK) {
        LOG_DRV_ERROR("sht3x: reset failed\r\n");
        i2c_driver_destroy(s_sht3x_instance);
        s_sht3x_instance = NULL;
        return ret;
    }
    
    return AICAM_OK;
}

int sht3x_read_measurement(float *temperature, float *humidity)
{
    return sht3x_read_measurement_cmd(SHT3X_CMD_MEASURE_SINGLE_H, temperature, humidity);
}

int sht3x_read_measurement_cmd(uint16_t cmd, float *temperature, float *humidity)
{
    uint8_t data[6];
    uint8_t cmd_buf[2];
    uint16_t temp_raw, hum_raw;
    uint8_t crc;
    int ret;
    uint32_t delay_ms;
    
    if (temperature == NULL || humidity == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Prepare command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(cmd >> 8);
    cmd_buf[1] = (uint8_t)(cmd & 0xFF);
    
    /* Send measurement command */
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    /* Wait for measurement to complete */
    delay_ms = sht3x_get_delay(cmd);
    osDelay(delay_ms);
    
    /* Read 6 bytes: temp MSB, temp LSB, temp CRC, hum MSB, hum LSB, hum CRC */
    ret = i2c_driver_read_data(s_sht3x_instance, data, 6, SHT3X_I2C_TIMEOUT_MS);
    if (ret != 6) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    /* Verify temperature CRC */
    crc = sht3x_crc8(data, 2);
    if (crc != data[2]) {
        LOG_DRV_ERROR("sht3x: temperature CRC mismatch (calc: 0x%02X, recv: 0x%02X)\r\n", crc, data[2]);
        return AICAM_ERROR_CHECKSUM;
    }
    
    /* Verify humidity CRC */
    crc = sht3x_crc8(&data[3], 2);
    if (crc != data[5]) {
        LOG_DRV_ERROR("sht3x: humidity CRC mismatch (calc: 0x%02X, recv: 0x%02X)\r\n", crc, data[5]);
        return AICAM_ERROR_CHECKSUM;
    }
    
    /* Convert raw values */
    temp_raw = (uint16_t)((data[0] << 8) | data[1]);
    hum_raw = (uint16_t)((data[3] << 8) | data[4]);
    
    /* Convert to temperature: T = -45 + 175 * (ST / 65535) */
    *temperature = -45.0f + (175.0f * (float)temp_raw / 65535.0f);
    
    /* Convert to humidity: RH = 100 * (SRH / 65535) */
    *humidity = 100.0f * (float)hum_raw / 65535.0f;
    
    return AICAM_OK;
}

int sht3x_start_periodic(uint16_t cmd)
{
    uint8_t cmd_buf[2];
    int ret;
    
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Check if it's a periodic command */
    if (cmd < SHT3X_CMD_MEASURE_PERIODIC_05_H || cmd > SHT3X_CMD_MEASURE_PERIODIC_10_L) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    /* Prepare command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(cmd >> 8);
    cmd_buf[1] = (uint8_t)(cmd & 0xFF);
    
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    return AICAM_OK;
}

int sht3x_fetch_data(float *temperature, float *humidity)
{
    uint8_t data[6];
    uint8_t cmd_buf[2];
    uint16_t temp_raw, hum_raw;
    uint8_t crc;
    int ret;
    
    if (temperature == NULL || humidity == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Prepare fetch command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(SHT3X_CMD_FETCH_DATA >> 8);
    cmd_buf[1] = (uint8_t)(SHT3X_CMD_FETCH_DATA & 0xFF);
    
    /* Send fetch data command */
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    /* Read 6 bytes: temp MSB, temp LSB, temp CRC, hum MSB, hum LSB, hum CRC */
    ret = i2c_driver_read_data(s_sht3x_instance, data, 6, SHT3X_I2C_TIMEOUT_MS);
    if (ret != 6) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    /* Verify temperature CRC */
    crc = sht3x_crc8(data, 2);
    if (crc != data[2]) {
        LOG_DRV_ERROR("sht3x: temperature CRC mismatch (calc: 0x%02X, recv: 0x%02X)\r\n", crc, data[2]);
        return AICAM_ERROR_CHECKSUM;
    }
    
    /* Verify humidity CRC */
    crc = sht3x_crc8(&data[3], 2);
    if (crc != data[5]) {
        LOG_DRV_ERROR("sht3x: humidity CRC mismatch (calc: 0x%02X, recv: 0x%02X)\r\n", crc, data[5]);
        return AICAM_ERROR_CHECKSUM;
    }
    
    /* Convert raw values */
    temp_raw = (uint16_t)((data[0] << 8) | data[1]);
    hum_raw = (uint16_t)((data[3] << 8) | data[4]);
    
    /* Convert to temperature: T = -45 + 175 * (ST / 65535) */
    *temperature = -45.0f + (175.0f * (float)temp_raw / 65535.0f);
    
    /* Convert to humidity: RH = 100 * (SRH / 65535) */
    *humidity = 100.0f * (float)hum_raw / 65535.0f;
    
    return AICAM_OK;
}

int sht3x_stop_periodic(void)
{
    uint8_t cmd_buf[2];
    int ret;
    
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Prepare break command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(SHT3X_CMD_BREAK >> 8);
    cmd_buf[1] = (uint8_t)(SHT3X_CMD_BREAK & 0xFF);
    
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    return AICAM_OK;
}

int sht3x_reset(void)
{
    uint8_t cmd_buf[2];
    int ret;
    
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Prepare reset command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(SHT3X_CMD_SOFT_RESET >> 8);
    cmd_buf[1] = (uint8_t)(SHT3X_CMD_SOFT_RESET & 0xFF);
    
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    osDelay(SHT3X_RESET_DELAY_MS);
    
    return AICAM_OK;
}

int sht3x_set_heater(bool enable)
{
    uint8_t cmd_buf[2];
    uint16_t cmd;
    int ret;
    
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    cmd = enable ? SHT3X_CMD_HEATER_ENABLE : SHT3X_CMD_HEATER_DISABLE;
    
    /* Prepare command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(cmd >> 8);
    cmd_buf[1] = (uint8_t)(cmd & 0xFF);
    
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    return AICAM_OK;
}

int sht3x_read_status(uint16_t *status)
{
    uint8_t data[3];
    uint8_t cmd_buf[2];
    uint8_t crc;
    int ret;
    
    if (status == NULL) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Prepare read status command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(SHT3X_CMD_READ_STATUS >> 8);
    cmd_buf[1] = (uint8_t)(SHT3X_CMD_READ_STATUS & 0xFF);
    
    /* Send read status command */
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    /* Read 3 bytes: status MSB, status LSB, CRC */
    ret = i2c_driver_read_data(s_sht3x_instance, data, 3, SHT3X_I2C_TIMEOUT_MS);
    if (ret != 3) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    /* Verify CRC */
    crc = sht3x_crc8(data, 2);
    if (crc != data[2]) {
        LOG_DRV_ERROR("sht3x: status CRC mismatch (calc: 0x%02X, recv: 0x%02X)\r\n", crc, data[2]);
        return AICAM_ERROR_CHECKSUM;
    }
    
    *status = (uint16_t)((data[0] << 8) | data[1]);
    
    return AICAM_OK;
}

int sht3x_clear_status(void)
{
    uint8_t cmd_buf[2];
    int ret;
    
    if (s_sht3x_instance == NULL) {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    
    /* Prepare clear status command bytes (MSB first) */
    cmd_buf[0] = (uint8_t)(SHT3X_CMD_CLEAR_STATUS >> 8);
    cmd_buf[1] = (uint8_t)(SHT3X_CMD_CLEAR_STATUS & 0xFF);
    
    ret = i2c_driver_write_data(s_sht3x_instance, cmd_buf, 2, SHT3X_I2C_TIMEOUT_MS);
    if (ret <= 0) {
        return (ret < 0) ? ret : AICAM_ERROR_IO;
    }
    
    return AICAM_OK;
}

void sht3x_deinit(void)
{
    if (s_sht3x_instance == NULL) {
        return;
    }
    
    i2c_driver_destroy(s_sht3x_instance);
    s_sht3x_instance = NULL;
    /* I2C port deinit is caller's responsibility. */
}
