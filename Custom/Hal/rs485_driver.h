#ifndef RS485_DRIVER_H
#define RS485_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usart.h"
#include "cmsis_os2.h"

#define RS485_DE_PIN                GPIO_PIN_2
#define RS485_DE_PORT               GPIOE
#define RS485_DE_CLK_ENABLE()       __HAL_RCC_GPIOE_CLK_ENABLE()
#define RS485_RE_PIN                GPIO_PIN_3
#define RS485_RE_PORT               GPIOB
#define RS485_RE_CLK_ENABLE()       __HAL_RCC_GPIOB_CLK_ENABLE()
#define RS485_ENABLE_RX()           {HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET); HAL_GPIO_WritePin(RS485_RE_PORT, RS485_RE_PIN, GPIO_PIN_RESET);}
#define RS485_ENABLE_TX()           {HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET); HAL_GPIO_WritePin(RS485_RE_PORT, RS485_RE_PIN, GPIO_PIN_SET);}
#define RS485_RX_TASK_STACK_SIZE    (4096 * 4)
#define RS485_RX_TASK_PRIORITY      (osPriorityNormal)
#define RS485_RX_QUEUE_SIZE         (8)
#define RS485_RX_BUFFER_SIZE        (2048)
#define RS485_TX_BUFFER_SIZE        (2048)

/**
 * @brief The RX callback function
 * @param data The received data
 * @param length The length of the received data
 */
typedef void (*rs485_rx_callback_t)(uint8_t *data, uint16_t length);

/**
 * @brief Initialize the RS485 driver
 * @param baudrate Uart baudrate, e.g. 115200
 * @param config Uart config, wordlength, stopbits, parity, e.g. "8N1"
 * @return 0 on success, otherwise error code
 */
int rs485_driver_init(uint32_t baudrate, const char *config);
/**
 * @brief Set the RX callback function
 * @param callback The callback function
 * @return 0 on success, otherwise error code
 */
int rs485_driver_set_rx_callback(rs485_rx_callback_t callback);
/**
 * @brief Send data through the RS485 communication
 * @param data The data to send
 * @param length The length of the data
 * @param timeout The timeout in milliseconds
 * @return 0 on success, otherwise error code
 */
int rs485_driver_send_data(uint8_t *data, uint16_t length, uint32_t timeout);
/**
 * @brief Receive data from the RS485 communication
 * @param data The data to receive
 * @param length The length of the data
 * @param timeout The timeout in milliseconds
 * @return >0 on success, otherwise error code
 */
int rs485_driver_receive_data(uint8_t *data, uint16_t length, uint32_t timeout);
/**
 * @brief Deinitialize the RS485 driver 
 * @return 0 on success, otherwise error code
 */
int rs485_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
