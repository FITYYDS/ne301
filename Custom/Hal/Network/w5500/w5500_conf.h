#ifndef __W5500_CONF_H__
#define __W5500_CONF_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include "spi.h"
#include "eth_tool.h"
#include "cmsis_os2.h"

#define __DEF_W5500_DBG__           (1)
#if __DEF_W5500_DBG__ > 0
    #define W5500_LOGE(fmt, ...)    printf("E [W5500]"fmt"\r\n", ##__VA_ARGS__)
#else
    #define W5500_LOGE(fmt, ...)
#endif
#if __DEF_W5500_DBG__ > 1
    #define W5500_LOGD(fmt, ...)    printf("D [W5500]"fmt"\r\n", ##__VA_ARGS__)
#else
    #define W5500_LOGD(fmt, ...)
#endif

#define	W5500_IS_USE_RTOS           (1)
#define	W5500_LOCK_TIMEOUT          (10000)

#define	W5500_SPI_LESS_10B_TIMEOUT  (100)
#define	W5500_SPI_MAX_TIMEOUT       (1000)

#define	W5500_SOCK_MAX_NUM		    (8)

/***********Interface to be implemented**********/
#define W5500_DELAY_US(us)          
#define W5500_DELAY_MS(ms)          osDelay(ms)

#define W5500_CSn_CLK_ENABLE()      __HAL_RCC_GPIOB_CLK_ENABLE()
#define W5500_GPIO_CSn_PORT         GPIOB
#define W5500_GPIO_CSn_PIN          GPIO_PIN_12
#define W5500_GPIO_CSn_HIGH()       HAL_GPIO_WritePin(W5500_GPIO_CSn_PORT, W5500_GPIO_CSn_PIN, GPIO_PIN_SET)
#define W5500_GPIO_CSn_LOW()        HAL_GPIO_WritePin(W5500_GPIO_CSn_PORT, W5500_GPIO_CSn_PIN, GPIO_PIN_RESET)

#define W5500_INTn_CLK_ENABLE()     __HAL_RCC_GPIOD_CLK_ENABLE()
#define W5500_GPIO_INTn_PORT        GPIOD
#define W5500_GPIO_INTn_PIN         GPIO_PIN_15
#define W5500_GPIO_INTn_READ()      HAL_GPIO_ReadPin(W5500_GPIO_INTn_PORT, W5500_GPIO_INTn_PIN)

#define W5500_RSTn_CLK_ENABLE()     __HAL_RCC_GPIOF_CLK_ENABLE()
#define W5500_GPIO_RSTn_PORT        GPIOF
#define W5500_GPIO_RSTn_PIN         GPIO_PIN_4
#define W5500_GPIO_RST_HIGH()       HAL_GPIO_WritePin(W5500_GPIO_RSTn_PORT, W5500_GPIO_RSTn_PIN, GPIO_PIN_SET)
#define W5500_GPIO_RST_LOW()        HAL_GPIO_WritePin(W5500_GPIO_RSTn_PORT, W5500_GPIO_RSTn_PIN, GPIO_PIN_RESET)

#define W5500_ISR_DISABLE()
#define W5500_ISR_ENABLE()
/*************************************************/
/// @brief W5500 error codes
typedef enum
{
    W5500_OK = 0x00,
    W5500_ERR_INVALID_ARG = -0x5F,
    W5500_ERR_INVALID_STATE,
    W5500_ERR_INVALID_SIZE,
    W5500_ERR_NOT_SUPPORT,
    W5500_ERR_SPI_FAILED,
    W5500_ERR_FAILED,
    W5500_ERR_CHECK,
    W5500_ERR_MEM,
    W5500_ERR_TIMEOUT,
    W5500_ERR_UNCONNECTED,
    W5500_ERR_UNKNOW,
} w5500_err_t;
#pragma pack(1)
/// @brief W5500 configuration structure
typedef struct
{
    uint8_t mac[6];     // MAC address
    uint8_t sub[4];     // Subnet mask
    uint8_t gw[4];      // Gateway address
    uint8_t ip[4];      // IP address

    uint16_t rtr;       // Retry timeout value (unit: 100us)
    uint8_t rcr;        // Maximum retry count
    uint8_t tx_size[W5500_SOCK_MAX_NUM];        // Socket TX buffer memory allocation
    uint8_t rx_size[W5500_SOCK_MAX_NUM];        // Socket RX buffer memory allocation
} w5500_config_t;
#pragma pack()
/// @brief W5500 default configuration
extern const w5500_config_t w5500_default_config;

#ifdef __cplusplus
}
#endif

#endif /* __W5500_CONF_H__ */
