/**
 * @file lsm6dsr.h
 * @brief LSM6DSR 6-axis IMU sensor driver (I2C) - accelerometer and gyroscope.
 *        Device on I2C port 1, 7-bit address 0x6A (SA0=L) or 0x6B (SA0=H).
 */

#ifndef LSM6DSR_H
#define LSM6DSR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** I2C 7-bit address (SA0 pin low) */
#define LSM6DSR_I2C_ADDR_LOW          (0x6AU)
/** I2C 7-bit address (SA0 pin high) */
#define LSM6DSR_I2C_ADDR_HIGH         (0x6BU)
/** I2C 7-bit address (default) */
#define LSM6DSR_I2C_ADDR_DEFAULT      LSM6DSR_I2C_ADDR_LOW

/** Expected device ID */
#define LSM6DSR_DEVICE_ID             (0x6BU)

/** Accelerometer full scale ranges */
typedef enum {
    LSM6DSR_ACC_FS_2G = 0,      /** ±2g */
    LSM6DSR_ACC_FS_4G,          /** ±4g */
    LSM6DSR_ACC_FS_8G,          /** ±8g */
    LSM6DSR_ACC_FS_16G,         /** ±16g */
} lsm6dsr_acc_fs_t;

/** Gyroscope full scale ranges */
typedef enum {
    LSM6DSR_GYRO_FS_125DPS = 0, /** ±125 dps */
    LSM6DSR_GYRO_FS_250DPS,     /** ±250 dps */
    LSM6DSR_GYRO_FS_500DPS,     /** ±500 dps */
    LSM6DSR_GYRO_FS_1000DPS,    /** ±1000 dps */
    LSM6DSR_GYRO_FS_2000DPS,    /** ±2000 dps */
} lsm6dsr_gyro_fs_t;

/** Output data rate (ODR) settings */
typedef enum {
    LSM6DSR_ODR_POWER_DOWN = 0,
    LSM6DSR_ODR_12Hz5,
    LSM6DSR_ODR_26Hz,
    LSM6DSR_ODR_52Hz,
    LSM6DSR_ODR_104Hz,
    LSM6DSR_ODR_208Hz,
    LSM6DSR_ODR_416Hz,
    LSM6DSR_ODR_833Hz,
    LSM6DSR_ODR_1666Hz,
    LSM6DSR_ODR_3332Hz,
    LSM6DSR_ODR_6664Hz,
} lsm6dsr_odr_t;

/** Sensor data structure */
typedef struct {
    float acc_x;        /** Acceleration X axis (mg) */
    float acc_y;        /** Acceleration Y axis (mg) */
    float acc_z;        /** Acceleration Z axis (mg) */
    float gyro_x;       /** Angular rate X axis (mdps) */
    float gyro_y;       /** Angular rate Y axis (mdps) */
    float gyro_z;       /** Angular rate Z axis (mdps) */
    float temperature;  /** Temperature (°C) */
} lsm6dsr_data_t;

/**
 * @brief Initialize LSM6DSR sensor
 * @param i2c_addr I2C 7-bit address (LSM6DSR_I2C_ADDR_LOW or LSM6DSR_I2C_ADDR_HIGH)
 * @return 0 on success, negative on error
 */
int lsm6dsr_init(uint8_t i2c_addr);

/**
 * @brief Deinitialize LSM6DSR sensor
 */
void lsm6dsr_deinit(void);

/**
 * @brief Check if sensor is initialized
 * @return true if initialized, false otherwise
 */
bool lsm6dsr_is_initialized(void);

/**
 * @brief Reset sensor
 * @return 0 on success, negative on error
 */
int lsm6dsr_reset(void);

/**
 * @brief Configure accelerometer
 * @param odr Output data rate
 * @param fs Full scale range
 * @return 0 on success, negative on error
 */
int lsm6dsr_acc_config(lsm6dsr_odr_t odr, lsm6dsr_acc_fs_t fs);

/**
 * @brief Configure gyroscope
 * @param odr Output data rate
 * @param fs Full scale range
 * @return 0 on success, negative on error
 */
int lsm6dsr_gyro_config(lsm6dsr_odr_t odr, lsm6dsr_gyro_fs_t fs);

/**
 * @brief Read sensor data
 * @param data Pointer to data structure to fill
 * @return 0 on success, negative on error
 */
int lsm6dsr_read_data(lsm6dsr_data_t *data);

/**
 * @brief Check if accelerometer data is ready
 * @param ready Pointer to store ready status
 * @return 0 on success, negative on error
 */
int lsm6dsr_acc_data_ready(bool *ready);

/**
 * @brief Check if gyroscope data is ready
 * @param ready Pointer to store ready status
 * @return 0 on success, negative on error
 */
int lsm6dsr_gyro_data_ready(bool *ready);

/**
 * @brief Check if temperature data is ready
 * @param ready Pointer to store ready status
 * @return 0 on success, negative on error
 */
int lsm6dsr_temp_data_ready(bool *ready);

/**
 * @brief Get device ID
 * @param id Pointer to store device ID
 * @return 0 on success, negative on error
 */
int lsm6dsr_get_device_id(uint8_t *id);

#ifdef __cplusplus
}
#endif

#endif /* LSM6DSR_H */
