#ifndef __I2C_DRIVER_H__
#define __I2C_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os2.h"

#define I2C_DRIVER_PORT_MAX_INSTANCE_NUM            10
#define I2C_DRIVER_IS_ENABLE_DMA                     0
#define I2C_DRIVER_DMA_THRESHOLD                     4  /* Use DMA only when transfer size > 4 bytes */

/* I2C port */
typedef enum {
    I2C_PORT_1 = 0,
    I2C_PORT_MAX = 1,
} i2c_port_t;

/* I2C address mode */
typedef enum {
    I2C_ADDRESS_7BIT = 0,
    I2C_ADDRESS_10BIT = 1,
} i2c_address_t;

/* I2C instance */
typedef struct {
    i2c_port_t port;
    uint16_t dev_addr;
    i2c_address_t address_mode;
} i2c_instance_t;

/* I2C driver functions */
int i2c_driver_init(i2c_port_t port);
void i2c_driver_deinit(i2c_port_t port);

i2c_instance_t *i2c_driver_create(i2c_port_t port, uint16_t dev_addr, i2c_address_t address_mode);
void i2c_driver_destroy(i2c_instance_t *instance);

int i2c_driver_is_ready(i2c_instance_t *instance, uint32_t trials, uint32_t timeout);
int i2c_driver_write_reg8(i2c_instance_t *instance, uint8_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout);
int i2c_driver_read_reg8(i2c_instance_t *instance, uint8_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout);
int i2c_driver_write_reg16(i2c_instance_t *instance, uint16_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout);
int i2c_driver_read_reg16(i2c_instance_t *instance, uint16_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout);

int i2c_driver_write_data(i2c_instance_t *instance, uint8_t *data, uint16_t size, uint32_t timeout);
int i2c_driver_read_data(i2c_instance_t *instance, uint8_t *data, uint16_t size, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
