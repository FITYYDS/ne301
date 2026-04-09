/**
 * @file nau881x_dev.h
 * @brief NAU881x (NAU8810/NAU8814) audio codec system adaptation layer.
 *
 * Wraps the low-level nau881x driver with the project's I2C driver and
 * provides a simple init/deinit API used by the command layer.
 *
 * NAU881x I2C protocol:
 *   Each transfer is 2 bytes:
 *     Byte 0: [reg_addr[6:0] | data[8]]   (7-bit reg addr in bits[7:1], data MSB in bit[0])
 *     Byte 1: data[7:0]
 */

#ifndef NAU881X_DEV_H
#define NAU881X_DEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "nau881x.h"

/**
 * @brief Initialize the NAU881x device.
 *
 * Caller must have called i2c_driver_init(I2C_PORT_1) beforehand.
 * Creates an I2C instance for the codec, verifies device presence,
 * and calls NAU881x_Init().
 *
 * @return AICAM_OK on success, AICAM_ERROR_ALREADY_INITIALIZED if already
 *         inited, or a negative aicam_result_t on failure.
 */
int nau881x_dev_init(void);

/**
 * @brief Deinitialize the NAU881x device and release the I2C instance.
 *
 * Caller is responsible for calling i2c_driver_deinit(I2C_PORT_1) afterwards
 * if the bus is no longer needed.
 */
void nau881x_dev_deinit(void);

/**
 * @brief Get a pointer to the NAU881x handle (for direct API calls).
 *
 * @return Pointer to the internal NAU881x_t handle, or NULL if not initialized.
 */
NAU881x_t *nau881x_dev_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* NAU881X_DEV_H */
