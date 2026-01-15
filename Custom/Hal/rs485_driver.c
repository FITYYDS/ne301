#include "rs485_driver.h"
#include "common_utils.h"
#include "aicam_types.h"
#include "Hal/mem.h"
#include "Log/debug.h"

#define RS485_EVENT_FLAGS_RX_COMPLETE       (1 << 0)
#define RS485_EVENT_FLAGS_RX_ERROR          (1 << 1)
#define RS485_EVENT_FLAGS_TX_COMPLETE       (1 << 2)
#define RS485_EVENT_FLAGS_RX_UNLOCKED       (1 << 3)
#define RS485_EVENT_FLAGS_TX_UNLOCKED       (1 << 4)
#define RS485_EVENT_FLAGS_TASK_EXIT_REQ     (1 << 5)
#define RS485_EVENT_FLAGS_TASK_EXIT_ACK     (1 << 6)

static uint8_t rx_buffer[RS485_RX_BUFFER_SIZE] ALIGN_32 UNCACHED = {0};
static uint8_t tx_buffer[RS485_TX_BUFFER_SIZE] ALIGN_32 UNCACHED = {0};
static rs485_rx_callback_t rx_callback = NULL;
static osThreadId_t rx_task_handle = NULL;
static uint8_t rx_task_stack[RS485_RX_TASK_STACK_SIZE] ALIGN_32 IN_PSRAM = {0};
static const osThreadAttr_t rx_task_attributes = {
    .name = "rs485_rx_task",
    .priority = RS485_RX_TASK_PRIORITY,
    .stack_mem = rx_task_stack,
    .stack_size = sizeof(rx_task_stack),
};
static osEventFlagsId_t rs485_event_flags = NULL;
static HAL_StatusTypeDef rx_status = HAL_ERROR;

void HAL_UART3_TxCpltCallback(UART_HandleTypeDef *huart)
{
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TX_COMPLETE);
}

void HAL_UART3_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->RxEventType == HAL_UART_RXEVENT_HT) return;
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_RX_COMPLETE);
}

void HAL_UART3_ErrorCallback(UART_HandleTypeDef *huart)
{
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_RX_ERROR);
}

static void rs485_rx_task(void *argument)
{
    int event = 0;
    uint16_t rlen = 0, remain_len = 0;

    while (1) {
        if (rx_status == HAL_OK) {
            /* Wait for the event */
            event = osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_RX_COMPLETE | RS485_EVENT_FLAGS_RX_ERROR | RS485_EVENT_FLAGS_TASK_EXIT_REQ, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
            if (event & osFlagsError) {
                HAL_UART_AbortReceive(&huart3);
            } else {
                remain_len = (uint16_t) __HAL_DMA_GET_COUNTER(huart3.hdmarx);
                rlen = RS485_RX_BUFFER_SIZE - remain_len;
                if (rlen > 0 && rx_callback) rx_callback(rx_buffer, rlen);
                if (event & RS485_EVENT_FLAGS_RX_ERROR) LOG_DRV_ERROR("[rs485_driver]Uart error: 0x%02X\r\n", huart3.ErrorCode);
                if (event & RS485_EVENT_FLAGS_TASK_EXIT_REQ) {
                    rx_status = HAL_ERROR;
                    break;
                }
            }
        }
        /* Wait for the RX to be unlocked */
        osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_RX_UNLOCKED, osFlagsWaitAll, osWaitForever);
        osEventFlagsClear(rs485_event_flags, RS485_EVENT_FLAGS_RX_COMPLETE | RS485_EVENT_FLAGS_RX_ERROR);
        rx_status = HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_buffer, RS485_RX_BUFFER_SIZE);
        osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_RX_UNLOCKED);
        if (rx_status != HAL_OK) {
            LOG_DRV_ERROR("[rs485_driver]Start receive error: 0x%02X\r\n", rx_status);
            HAL_UART_AbortReceive(&huart3);
            osDelay(200);
        }
    }
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TASK_EXIT_ACK);
    osThreadExit();
}

int rs485_driver_init(uint32_t baudrate, const char *config)
{
    Uart_Config_t uart_config = {
        .baudrate = baudrate,
        .wordlength = UART_WORDLENGTH_8B,
        .stopbits = UART_STOPBITS_1,
        .parity = UART_PARITY_NONE,
    };
    GPIO_InitTypeDef GPIO_InitStruct = {
        .Pin = RS485_RE_PIN,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
    };

    /* Check if the RS485 driver is already initialized */
    if (rx_task_handle != NULL) {
        LOG_DRV_ERROR("[rs485_driver]RS485 driver already initialized\r\n");
        return AICAM_ERROR_ALREADY_RUNNING;
    }
    /* Check the baudrate */
    if (baudrate < 1200 || baudrate > 115200) {
        LOG_DRV_ERROR("[rs485_driver]Invalid baudrate: %d\r\n", baudrate);
        return AICAM_ERROR_INVALID_PARAM;
    }
    /* Parse the config */
    if (config) {
        if (strlen(config) != 3) {
            LOG_DRV_ERROR("[rs485_driver]Invalid config: %s\r\n", config);
            return AICAM_ERROR_INVALID_PARAM;
        }
        /* Wordlength */
        uart_config.wordlength = config[0] - '0';
        if (uart_config.wordlength == 7) {
            uart_config.wordlength = UART_WORDLENGTH_7B;
        } else if (uart_config.wordlength == 8) {
            uart_config.wordlength = UART_WORDLENGTH_8B;
        } else if (uart_config.wordlength == 9) {
            uart_config.wordlength = UART_WORDLENGTH_9B;
        } else {
            LOG_DRV_ERROR("[rs485_driver]Invalid wordlength: %s\r\n", config);
            return AICAM_ERROR_INVALID_PARAM;
        }
        /* Parity */
        if (config[1] == 'N' || config[1] == 'n') {
            uart_config.parity = UART_PARITY_NONE;
        } else if (config[1] == 'E' || config[1] == 'e') {
            uart_config.parity = UART_PARITY_EVEN;
        } else if (config[1] == 'O' || config[1] == 'o') {
            uart_config.parity = UART_PARITY_ODD;
        } else {
            LOG_DRV_ERROR("[rs485_driver]Invalid parity: %s\r\n", config);
            return AICAM_ERROR_INVALID_PARAM;
        }
        /* Stopbits */
        if (config[2] == '1') {
            uart_config.stopbits = UART_STOPBITS_1;
        } else if (config[2] == '2') {
            uart_config.stopbits = UART_STOPBITS_2;
        } else {
            LOG_DRV_ERROR("[rs485_driver]Invalid stopbits: %s\r\n", config);
            return AICAM_ERROR_INVALID_PARAM;
        }
    }
    /* Reset runtime state in case of re-init after a previous deinit */
    rx_status = HAL_ERROR;
    rx_callback = NULL;

    rs485_event_flags = osEventFlagsNew(NULL);
    if (rs485_event_flags == NULL) {
        LOG_DRV_ERROR("[rs485_driver]Failed to create event flags\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TX_UNLOCKED | RS485_EVENT_FLAGS_RX_UNLOCKED);
    MX_USART3_UART_Init(&uart_config);
    RS485_RE_CLK_ENABLE();
    HAL_GPIO_Init(RS485_RE_PORT, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = RS485_DE_PIN;
    RS485_DE_CLK_ENABLE();
    HAL_GPIO_Init(RS485_DE_PORT, &GPIO_InitStruct);
    RS485_ENABLE_RX();
    rx_task_handle = osThreadNew(rs485_rx_task, NULL, &rx_task_attributes);
    if (rx_task_handle == NULL) {
        HAL_UART_DeInit(&huart3);
        HAL_GPIO_DeInit(RS485_RE_PORT, RS485_RE_PIN);
        HAL_GPIO_DeInit(RS485_DE_PORT, RS485_DE_PIN);
        osEventFlagsDelete(rs485_event_flags);
        rs485_event_flags = NULL;
        LOG_DRV_ERROR("[rs485_driver]Failed to create rx task\r\n");
        return AICAM_ERROR_NO_MEMORY;
    }
    return AICAM_OK;
}

int rs485_driver_set_rx_callback(rs485_rx_callback_t callback)
{
    if (callback == NULL) {
        LOG_DRV_ERROR("[rs485_driver]Invalid callback\r\n");
        return AICAM_ERROR_INVALID_PARAM;
    }
    rx_callback = callback;
    return AICAM_OK;
}

int rs485_driver_send_data(uint8_t *data, uint16_t length, uint32_t timeout)
{
    int ret = 0;
    if (rx_task_handle == NULL) {
        LOG_DRV_ERROR("[rs485_driver]RS485 driver not initialized\r\n");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    if (data == NULL || length == 0 || length > RS485_TX_BUFFER_SIZE) {
        LOG_DRV_ERROR("[rs485_driver]Invalid data or length\r\n");
        return AICAM_ERROR_INVALID_PARAM;
    }

    ret = osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_TX_UNLOCKED, osFlagsWaitAll, timeout);
    if (ret & osFlagsError) {
        LOG_DRV_ERROR("[rs485_driver]Failed to wait for tx unlock\r\n");
        return AICAM_ERROR_TIMEOUT;
    }
    osEventFlagsClear(rs485_event_flags, RS485_EVENT_FLAGS_TX_COMPLETE);

    /* Enable RS485 TX mode */
    RS485_ENABLE_TX();
    memcpy(tx_buffer, data, length);
    ret = HAL_UART_Transmit_DMA(&huart3, tx_buffer, length);
    if (ret != HAL_OK) {
        LOG_DRV_ERROR("[rs485_driver]Failed to send data: 0x%02X\r\n", ret);
        RS485_ENABLE_RX();
        osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TX_UNLOCKED);
        return AICAM_ERROR_HAL_IO;
    }
    
    ret = osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_TX_COMPLETE, osFlagsWaitAll, timeout);
    if (ret & osFlagsError) {
        LOG_DRV_ERROR("[rs485_driver]Failed to wait for tx complete\r\n");
        RS485_ENABLE_RX();
        osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TX_UNLOCKED);
        return AICAM_ERROR_TIMEOUT;
    }

    /* Enable RS485 RX mode after transmission */
    RS485_ENABLE_RX();
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TX_UNLOCKED);
    return AICAM_OK;
}

int rs485_driver_receive_data(uint8_t *data, uint16_t length, uint32_t timeout)
{
    int ret = 0;
    uint16_t remain_len = 0, rlen = 0;
    if (rx_task_handle == NULL) {
        LOG_DRV_ERROR("[rs485_driver]RS485 driver not initialized\r\n");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    if (data == NULL || length == 0 || length > RS485_RX_BUFFER_SIZE) {
        LOG_DRV_ERROR("[rs485_driver]Invalid data or length\r\n");
        return AICAM_ERROR_INVALID_PARAM;
    }

    ret = osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_RX_UNLOCKED, osFlagsWaitAll, timeout);
    if (ret & osFlagsError) {
        LOG_DRV_ERROR("[rs485_driver]Failed to wait for rx unlock\r\n");
        return AICAM_ERROR_TIMEOUT;
    }
    
    ret = osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_RX_COMPLETE | RS485_EVENT_FLAGS_RX_ERROR, osFlagsWaitAny, timeout);
    if (ret & osFlagsError) ret = AICAM_ERROR_TIMEOUT;
    else if (ret & RS485_EVENT_FLAGS_RX_ERROR) ret = AICAM_ERROR_HAL_IO;
    else if (ret & RS485_EVENT_FLAGS_RX_COMPLETE) {
        remain_len = (uint16_t) __HAL_DMA_GET_COUNTER(huart3.hdmarx);
        rlen = RS485_RX_BUFFER_SIZE - remain_len;
        ret = rlen > length ? length : rlen;
        memcpy(data, rx_buffer, ret);
    } else ret = AICAM_ERROR_UNKNOWN;

    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_RX_UNLOCKED);
    return ret;
}

int rs485_driver_deinit(void)
{
    if (rx_task_handle == NULL) {
        LOG_DRV_ERROR("[rs485_driver]RS485 driver not initialized\r\n");
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    osEventFlagsSet(rs485_event_flags, RS485_EVENT_FLAGS_TASK_EXIT_REQ);
    osEventFlagsWait(rs485_event_flags, RS485_EVENT_FLAGS_TASK_EXIT_ACK | RS485_EVENT_FLAGS_RX_UNLOCKED | RS485_EVENT_FLAGS_TX_UNLOCKED, osFlagsWaitAll, osWaitForever);
    osThreadTerminate(rx_task_handle);
    rx_task_handle = NULL;
    HAL_UART_DeInit(&huart3);
    HAL_GPIO_DeInit(RS485_RE_PORT, RS485_RE_PIN);
    HAL_GPIO_DeInit(RS485_DE_PORT, RS485_DE_PIN);
    osEventFlagsDelete(rs485_event_flags);
    rs485_event_flags = NULL;
    return AICAM_OK;
}
