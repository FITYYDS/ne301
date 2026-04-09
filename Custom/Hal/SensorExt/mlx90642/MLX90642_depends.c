/**
 * @file MLX90642_depends.c
 * @brief MLX90642 I2C platform abstraction layer for STM32N6 / i2c_driver.
 *
 * Implements the three hooks required by the Melexis MLX90642 library:
 *   - MLX90642_I2CRead  — write 16-bit address then read N words (big-endian)
 *   - MLX90642_I2CWrite — raw byte write (opcode + payload already formed by library)
 *   - MLX90642_Wait_ms  — millisecond delay via CMSIS-RTOS
 */

#include "MLX90642_depends.h"
#include "MLX90642.h"
#include "../i2c_driver/i2c_driver.h"
#include "cmsis_os2.h"

#define MLX90642_I2C_TIMEOUT_MS     200U

/* Set/get by mlx90642_dev.c after i2c_driver_create() */
static i2c_instance_t *s_mlx90642_i2c_instance = NULL;

void mlx90642_depends_set_i2c_instance(i2c_instance_t *instance)
{
    s_mlx90642_i2c_instance = instance;
}

i2c_instance_t *mlx90642_depends_get_i2c_instance(void)
{
    return s_mlx90642_i2c_instance;
}

/**
 * Block read: MLX90642 protocol requires a REPEATED START between the address
 * phase and the data phase (no STOP in between).
 *
 * Transaction on the bus:
 *   START → [SA|W] → [addr_MSB][addr_LSB] → REPEATED START → [SA|R]
 *   → [data bytes (big-endian words)] → STOP
 *
 * i2c_driver_read_reg16 maps to HAL_I2C_Mem_Read(..., I2C_MEMADD_SIZE_16BIT, ...)
 * which generates exactly this sequence.
 *
 * @retval 0  success
 * @retval <0 error
 */
int MLX90642_I2CRead(uint8_t slaveAddr, uint16_t startAddress,
                     uint16_t nMemAddressRead, uint16_t *rData)
{
    (void)slaveAddr; /* address managed by i2c_instance_t */

    if (s_mlx90642_i2c_instance == NULL || rData == NULL || nMemAddressRead == 0) {
        return -MLX90642_NACK_ERR;
    }

    uint16_t byte_count = (uint16_t)(nMemAddressRead * 2U);
    uint8_t *rx_buf = (uint8_t *)rData;

    /* HAL_I2C_Mem_Read with 16-bit register address generates:
     *   START+ADDR_W + [reg_hi][reg_lo] + REPEATED_START+ADDR_R + [data...] + STOP */
    int ret = i2c_driver_read_reg16(s_mlx90642_i2c_instance, startAddress,
                                    rx_buf, byte_count, MLX90642_I2C_TIMEOUT_MS);
    if (ret < 0) {
        return -MLX90642_NACK_ERR;
    }

    /* Sensor transmits big-endian words; convert to host uint16_t in-place */
    for (uint16_t i = 0; i < nMemAddressRead; i++) {
        uint8_t hi = rx_buf[i * 2U];
        uint8_t lo = rx_buf[i * 2U + 1U];
        rData[i] = ((uint16_t)hi << 8) | lo;
    }

    return 0;
}

/**
 * Raw byte write: the library has already assembled the full I2C payload
 * (opcode + 16-bit address + 16-bit data, or just opcode + command word).
 *
 * @retval 0  success
 * @retval <0 error
 */
int MLX90642_I2CWrite(uint8_t slaveAddr, uint8_t *buffer, uint8_t bytesNum)
{
    (void)slaveAddr;

    if (s_mlx90642_i2c_instance == NULL || buffer == NULL || bytesNum == 0U) {
        return -MLX90642_NACK_ERR;
    }

    int ret = i2c_driver_write_data(s_mlx90642_i2c_instance, buffer, bytesNum,
                                    MLX90642_I2C_TIMEOUT_MS);
    if (ret < 0) {
        return -MLX90642_NACK_ERR;
    }

    return 0;
}

/**
 * Millisecond delay via CMSIS-RTOS osDelay.
 */
void MLX90642_Wait_ms(uint16_t time_ms)
{
    osDelay(time_ms);
}
