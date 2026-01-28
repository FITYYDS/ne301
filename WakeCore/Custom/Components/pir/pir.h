#ifndef __PIR_H
#define __PIR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sys_config.h"

#define PIR_CONFIG_RETRY_TIMES  10
#define PIR_SERIAL_HIGH         PIR_SERIAL_GPIO_Port->BSRR = (uint32_t)PIR_SERIAL_Pin
#define PIR_SERIAL_LOW          PIR_SERIAL_GPIO_Port->BRR = (uint32_t)PIR_SERIAL_Pin
#define PIR_TRIGGER_READ        (PIR_TRIGGER_GPIO_Port->IDR & (uint32_t)PIR_TRIGGER_Pin)

typedef struct
{
    uint8_t SENS;
    uint8_t BLIND;
    uint8_t PULSE;
    uint8_t WINDOW;
    uint8_t MOTION;
    uint8_t INT;
    uint8_t VOLT;
    uint8_t SUPP;
    uint8_t RSV;
} pir_config_t;

int pir_config(pir_config_t *config);
void pir_trigger_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __PIR_H */
