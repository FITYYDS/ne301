#include "string.h"
#include "i2c.h"
#include "debug.h"
#include "tx_api.h"
#include "Hal/mem.h"
#include "i2c_driver.h"
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
#include "main.h"  /* Includes stm32n6xx_hal.h which includes CMSIS core headers */
#endif

/* I2C driver event flags */
#define I2C_DRIVER_EVENT_TX_COMPLETE             0x00000001
#define I2C_DRIVER_EVENT_RX_COMPLETE             0x00000002
#define I2C_DRIVER_EVENT_ERROR                   0x00000004

/* I2C driver private data */
typedef struct {
    I2C_HandleTypeDef *hi2c;
    osMutexId_t mtx_id;
    osEventFlagsId_t event_flags_id;
    uint8_t instance_valid_list[I2C_DRIVER_PORT_MAX_INSTANCE_NUM];
    i2c_instance_t instance_list[I2C_DRIVER_PORT_MAX_INSTANCE_NUM];
} i2c_driver_priv_t;

/* I2C driver handle list */
static I2C_HandleTypeDef *hi2c_list[I2C_PORT_MAX] = {&hi2c1};

/* I2C driver global data */
static i2c_driver_priv_t i2c_driver_list[I2C_PORT_MAX] = {0};

/* Find unused instance */
static int i2c_driver_find_unused_instance(i2c_port_t port)
{
    if (i2c_driver_list[port].hi2c == NULL) return -1;
    for (int i = 0; i < I2C_DRIVER_PORT_MAX_INSTANCE_NUM; i++) {
        if (i2c_driver_list[port].instance_valid_list[i] == 0) return i;
    }
    return -1;
}

/* Find instance index */
static int i2c_driver_find_instance_index(i2c_port_t port, i2c_instance_t *instance)
{
    if (i2c_driver_list[port].hi2c == NULL) return -1;
    for (int i = 0; i < I2C_DRIVER_PORT_MAX_INSTANCE_NUM; i++) {
        if (i2c_driver_list[port].instance_valid_list[i] == 1 && &i2c_driver_list[port].instance_list[i] == instance) return i;
    }
    return -1;
}

/* Get I2C port */
i2c_port_t i2c_driver_get_port(I2C_HandleTypeDef *hi2c)
{
    for (int i = 0; i < I2C_PORT_MAX; i++) {
        if (hi2c == i2c_driver_list[i].hi2c) return (i2c_port_t)i;
    }
    return I2C_PORT_MAX;
}

/* Initialize I2C driver */
int i2c_driver_init(i2c_port_t port)
{
    TX_INTERRUPT_SAVE_AREA
    if (port >= I2C_PORT_MAX || port < 0) return AICAM_ERROR_INVALID_PARAM;

    TX_DISABLE
    if (i2c_driver_list[port].hi2c != NULL) {
        TX_RESTORE
        return AICAM_OK;
    }
    MX_I2C1_Init();
    i2c_driver_list[port].hi2c = hi2c_list[port];
    i2c_driver_list[port].mtx_id = osMutexNew(NULL);
    i2c_driver_list[port].event_flags_id = osEventFlagsNew(NULL);
    if (i2c_driver_list[port].mtx_id == NULL || i2c_driver_list[port].event_flags_id == NULL) {
        if (i2c_driver_list[port].mtx_id) osMutexDelete(i2c_driver_list[port].mtx_id);
        if (i2c_driver_list[port].event_flags_id) osEventFlagsDelete(i2c_driver_list[port].event_flags_id);
        memset(&i2c_driver_list[port], 0, sizeof(i2c_driver_priv_t));
        TX_RESTORE
        return AICAM_ERROR_NO_MEMORY;
    }
    TX_RESTORE
    return AICAM_OK;
}

/* Create I2C instance */
i2c_instance_t *i2c_driver_create(i2c_port_t port, uint16_t dev_addr, i2c_address_t address_mode)
{
    int instance_idx = -1;
    i2c_instance_t *instance = NULL;
    if (port >= I2C_PORT_MAX || port < 0) return NULL;
    if (i2c_driver_list[port].hi2c == NULL) return NULL;

    osMutexAcquire(i2c_driver_list[port].mtx_id, osWaitForever);
    // create instance
    instance_idx = i2c_driver_find_unused_instance(port);
    if (instance_idx < 0) {
        osMutexRelease(i2c_driver_list[port].mtx_id);
        return NULL;
    }
    instance = &i2c_driver_list[port].instance_list[instance_idx];
    instance->port = port;
    instance->dev_addr = dev_addr;
    instance->address_mode = address_mode;
    i2c_driver_list[port].instance_valid_list[instance_idx] = 1;
    osMutexRelease(i2c_driver_list[port].mtx_id);

    return instance;
}

/* Destroy I2C instance */
void i2c_driver_destroy(i2c_instance_t *instance)
{
    int instance_idx = -1;
    if (instance == NULL) return;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return;

    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    instance_idx = i2c_driver_find_instance_index(instance->port, instance);
    if (instance_idx < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return;
    }
    // destroy instance
    i2c_driver_list[instance->port].instance_valid_list[instance_idx] = 0;
    memset(&i2c_driver_list[instance->port].instance_list[instance_idx], 0, sizeof(i2c_instance_t));
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
}

void i2c_driver_deinit(i2c_port_t port)
{
    TX_INTERRUPT_SAVE_AREA
    if (port >= I2C_PORT_MAX || port < 0) return;
    if (i2c_driver_list[port].hi2c == NULL) return;

    osMutexAcquire(i2c_driver_list[port].mtx_id, osWaitForever);
    for (int j = 0; j < I2C_DRIVER_PORT_MAX_INSTANCE_NUM; j++) {
        i2c_driver_list[port].instance_valid_list[j] = 0;
        memset(&i2c_driver_list[port].instance_list[j], 0, sizeof(i2c_instance_t));
    }
    osMutexDelete(i2c_driver_list[port].mtx_id);
    osEventFlagsDelete(i2c_driver_list[port].event_flags_id);
    HAL_I2C_DeInit(i2c_driver_list[port].hi2c);
    TX_DISABLE
    memset(&i2c_driver_list[port], 0, sizeof(i2c_driver_priv_t));
    TX_RESTORE
}

/* Check if I2C instance is ready */
int i2c_driver_is_ready(i2c_instance_t *instance, uint32_t trials, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;

    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    status = HAL_I2C_IsDeviceReady(i2c_driver_list[instance->port].hi2c, instance->dev_addr, trials, timeout);
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_NOT_FOUND;
    return AICAM_OK;
}

/* Write 8-bit register */
int i2c_driver_write_reg8(i2c_instance_t *instance, uint8_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL || data == NULL || size == 0) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;

    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
#if I2C_DRIVER_IS_ENABLE_DMA
    if (size > I2C_DRIVER_DMA_THRESHOLD) {
        uint32_t event_flags;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
        /* Clean cache before DMA write to ensure data is written to memory */
        SCB_CleanDCache_by_Addr((uint32_t *)((uint32_t)data & ~(uint32_t)0x1F), 
                                 ((size + 31) & ~31));
#endif
        status = HAL_I2C_Mem_Write_DMA(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, data, size);
        if (status == HAL_OK) {
            event_flags = osEventFlagsWait(i2c_driver_list[instance->port].event_flags_id, I2C_DRIVER_EVENT_TX_COMPLETE | I2C_DRIVER_EVENT_ERROR, osFlagsWaitAny, timeout);
            if (event_flags & osFlagsError || event_flags & I2C_DRIVER_EVENT_ERROR) {
                LOG_DRV_ERROR("I2C instance(0x%02X) dma write reg8 failed, event_flags: 0x%08X\r\n", instance->dev_addr, event_flags);
                HAL_I2C_Master_Abort_IT(i2c_driver_list[instance->port].hi2c, instance->dev_addr);
                status = HAL_ERROR;
            }
        }
    } else {
        status = HAL_I2C_Mem_Write(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, data, size, timeout);
    }
#else
    status = HAL_I2C_Mem_Write(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, data, size, timeout);
#endif
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_HAL_IO;
    return size;
}

/* Read 8-bit register */
int i2c_driver_read_reg8(i2c_instance_t *instance, uint8_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL || data == NULL || size == 0) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;

    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
#if I2C_DRIVER_IS_ENABLE_DMA
    if (size > I2C_DRIVER_DMA_THRESHOLD) {
        uint32_t event_flags;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
        /* Invalidate cache before DMA read to ensure reading from memory */
        SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)data & ~(uint32_t)0x1F), 
                                      ((size + 31) & ~31));
#endif
        status = HAL_I2C_Mem_Read_DMA(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, data, size);
        if (status == HAL_OK) {
            event_flags = osEventFlagsWait(i2c_driver_list[instance->port].event_flags_id, I2C_DRIVER_EVENT_RX_COMPLETE | I2C_DRIVER_EVENT_ERROR, osFlagsWaitAny, timeout);
            if (event_flags & osFlagsError || event_flags & I2C_DRIVER_EVENT_ERROR) {
                LOG_DRV_ERROR("I2C instance(0x%02X) dma read reg8 failed, event_flags: 0x%08X\r\n", instance->dev_addr, event_flags);
                HAL_I2C_Master_Abort_IT(i2c_driver_list[instance->port].hi2c, instance->dev_addr);
                status = HAL_ERROR;
            } else {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
                /* Invalidate cache after DMA read to ensure CPU reads fresh data */
                SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)data & ~(uint32_t)0x1F), 
                                              ((size + 31) & ~31));
#endif
            }
        }
    } else {
        status = HAL_I2C_Mem_Read(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, data, size, timeout);
    }
#else
    status = HAL_I2C_Mem_Read(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_8BIT, data, size, timeout);
#endif
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_HAL_IO;
    return size;
}

/* Write 16-bit register */
int i2c_driver_write_reg16(i2c_instance_t *instance, uint16_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL || data == NULL || size == 0) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;
    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
#if I2C_DRIVER_IS_ENABLE_DMA
    if (size > I2C_DRIVER_DMA_THRESHOLD) {
        uint32_t event_flags;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
        /* Clean cache before DMA write to ensure data is written to memory */
        SCB_CleanDCache_by_Addr((uint32_t *)((uint32_t)data & ~(uint32_t)0x1F), 
                                 ((size + 31) & ~31));
#endif
        status = HAL_I2C_Mem_Write_DMA(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_16BIT, data, size);
        if (status == HAL_OK) {
            event_flags = osEventFlagsWait(i2c_driver_list[instance->port].event_flags_id, I2C_DRIVER_EVENT_TX_COMPLETE | I2C_DRIVER_EVENT_ERROR, osFlagsWaitAny, timeout);
            if (event_flags & osFlagsError || event_flags & I2C_DRIVER_EVENT_ERROR) {
                LOG_DRV_ERROR("I2C instance(0x%02X) dma write reg16 failed, event_flags: 0x%08X\r\n", instance->dev_addr, event_flags);
                HAL_I2C_Master_Abort_IT(i2c_driver_list[instance->port].hi2c, instance->dev_addr);
                status = HAL_ERROR;
            }
        }
    } else {
        status = HAL_I2C_Mem_Write(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_16BIT, data, size, timeout);
    }
#else
    status = HAL_I2C_Mem_Write(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_16BIT, data, size, timeout);
#endif
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_HAL_IO;
    return size;
}

/* Read 16-bit register */
int i2c_driver_read_reg16(i2c_instance_t *instance, uint16_t reg_addr, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL || data == NULL || size == 0) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;
    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
#if I2C_DRIVER_IS_ENABLE_DMA
    if (size > I2C_DRIVER_DMA_THRESHOLD) {
        uint32_t event_flags;
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
        /* Invalidate cache before DMA read to ensure reading from memory */
        SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)data & ~(uint32_t)0x1F), 
                                      ((size + 31) & ~31));
#endif
        status = HAL_I2C_Mem_Read_DMA(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_16BIT, data, size);
        if (status == HAL_OK) {
            event_flags = osEventFlagsWait(i2c_driver_list[instance->port].event_flags_id, I2C_DRIVER_EVENT_RX_COMPLETE | I2C_DRIVER_EVENT_ERROR, osFlagsWaitAny, timeout);
            if (event_flags & osFlagsError || event_flags & I2C_DRIVER_EVENT_ERROR) {
                LOG_DRV_ERROR("I2C instance(0x%02X) dma read reg16 failed, event_flags: 0x%08X\r\n", instance->dev_addr, event_flags);
                HAL_I2C_Master_Abort_IT(i2c_driver_list[instance->port].hi2c, instance->dev_addr);
                status = HAL_ERROR;
            } else {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT != 0U)
                /* Invalidate cache after DMA read to ensure CPU reads fresh data */
                SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)data & ~(uint32_t)0x1F), 
                                              ((size + 31) & ~31));
#endif
            }
        }
    } else {
        status = HAL_I2C_Mem_Read(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_16BIT, data, size, timeout);
    }
#else
    status = HAL_I2C_Mem_Read(i2c_driver_list[instance->port].hi2c, instance->dev_addr, reg_addr, I2C_MEMADD_SIZE_16BIT, data, size, timeout);
#endif
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_HAL_IO;
    return size;
}

int i2c_driver_write_data(i2c_instance_t *instance, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL || data == NULL || size == 0) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;
    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
#if I2C_DRIVER_IS_ENABLE_DMA
    if (size > I2C_DRIVER_DMA_THRESHOLD) {
        uint32_t event_flags;
        status = HAL_I2C_Master_Transmit_DMA(i2c_driver_list[instance->port].hi2c, instance->dev_addr, data, size);
    }
#else
    status = HAL_I2C_Master_Transmit(i2c_driver_list[instance->port].hi2c, instance->dev_addr, data, size, timeout);
#endif
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_HAL_IO;
    return size;
}

int i2c_driver_read_data(i2c_instance_t *instance, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    if (instance == NULL || data == NULL || size == 0) return AICAM_ERROR_INVALID_PARAM;
    if (instance->port >= I2C_PORT_MAX || instance->port < 0) return AICAM_ERROR_INVALID_PARAM;
    osMutexAcquire(i2c_driver_list[instance->port].mtx_id, osWaitForever);
    if (i2c_driver_find_instance_index(instance->port, instance) < 0) {
        osMutexRelease(i2c_driver_list[instance->port].mtx_id);
        return AICAM_ERROR_NOT_INITIALIZED;
    }
#if I2C_DRIVER_IS_ENABLE_DMA
    if (size > I2C_DRIVER_DMA_THRESHOLD) {
        uint32_t event_flags;
        status = HAL_I2C_Master_Receive_DMA(i2c_driver_list[instance->port].hi2c, instance->dev_addr, data, size);
    }
#else
    status = HAL_I2C_Master_Receive(i2c_driver_list[instance->port].hi2c, instance->dev_addr, data, size, timeout);
#endif
    osMutexRelease(i2c_driver_list[instance->port].mtx_id);
    if (status != HAL_OK) return AICAM_ERROR_HAL_IO;
    return size;
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    i2c_port_t port = i2c_driver_get_port(hi2c);
    if (port >= I2C_PORT_MAX || port < 0) return;
    osEventFlagsSet(i2c_driver_list[port].event_flags_id, I2C_DRIVER_EVENT_TX_COMPLETE);
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    i2c_port_t port = i2c_driver_get_port(hi2c);
    if (port >= I2C_PORT_MAX || port < 0) return;
    osEventFlagsSet(i2c_driver_list[port].event_flags_id, I2C_DRIVER_EVENT_RX_COMPLETE);
}

/* I2C memory transfer complete callback */
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    i2c_port_t port = i2c_driver_get_port(hi2c);
    if (port >= I2C_PORT_MAX || port < 0) return;
    osEventFlagsSet(i2c_driver_list[port].event_flags_id, I2C_DRIVER_EVENT_TX_COMPLETE);
}

/* I2C memory read complete callback */
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    i2c_port_t port = i2c_driver_get_port(hi2c);
    if (port >= I2C_PORT_MAX || port < 0) return;
    osEventFlagsSet(i2c_driver_list[port].event_flags_id, I2C_DRIVER_EVENT_RX_COMPLETE);
}

/* I2C error callback */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    i2c_port_t port = i2c_driver_get_port(hi2c);
    if (port >= I2C_PORT_MAX || port < 0) return;
    osEventFlagsSet(i2c_driver_list[port].event_flags_id, I2C_DRIVER_EVENT_ERROR);
}


