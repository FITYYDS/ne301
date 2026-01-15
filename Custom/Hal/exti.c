/*
 * ms_exti.c
 *
 *  Created on: Feb 6, 2023
 *      Author: Spider
 */


#include "exti.h"

typedef void (*exit_callback)(void);


exit_callback exti0_callback[5] = {NULL};
exit_callback exti2_callback[5] = {NULL};
exit_callback exti3_callback[5] = {NULL};
exit_callback exti4_callback[5] = {NULL};
exit_callback exti5_callback[5] = {NULL};
exit_callback exti8_callback[5] = {NULL};
exit_callback exti10_callback[5] = {NULL};
exit_callback exti11_callback[5] = {NULL};
exit_callback exti12_callback[5] = {NULL};
exit_callback exti13_callback[5] = {NULL};
exit_callback exti15_callback[5] = {NULL};

void all_exti_init(void)
{

}

/**
  * @brief  EXTI line detection callbacks.
  * @param  GPIO_Pin: Specifies the pins connected EXTI line
  * @retval None
  */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == GPIO_PIN_0)
	{
	    exti0_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_2)
	{
	    exti2_all_callback();
	}

    if (GPIO_Pin == GPIO_PIN_3)
	{
	    exti3_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_4)
	{
	    exti4_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_5)
	{
	    exti5_all_callback();
	}

    if (GPIO_Pin == GPIO_PIN_8)
	{
	    exti8_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_10)
	{
	    exti10_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_11)
	{
	    exti11_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_12)
	{
	    exti12_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_15)
	{
	    exti15_all_callback();
	}
}

/**
  * @brief  EXTI line detection callback.
  * @param  GPIO_Pin: Specifies the port pin connected to corresponding EXTI line.
  * @retval None
  */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == GPIO_PIN_0)
	{
	    exti0_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_2)
	{
	    exti2_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_3)
	{
	    exti3_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_4)
	{
	    exti4_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_5)
	{
	    exti5_all_callback();
	}

    if (GPIO_Pin == GPIO_PIN_8)
	{
	    exti8_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_10)
	{
	    exti10_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_11)
	{
	    exti11_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_12)
	{
	    exti12_all_callback();
	}

	if (GPIO_Pin == GPIO_PIN_15)
	{
	    exti15_all_callback();
	}
}

/*exit0*/
void exti0_irq_unregister(void *f)
{
    uint8_t i = 0;
    if (i < 5) {
        if (exti0_callback[i] == (exit_callback)f) {
            exti0_callback[i] = NULL;
        }
        i++;
    }
}

void exti0_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti0_callback[i++] = (exit_callback)f;
    }
}

void exti0_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti0_callback[i]) {
            exti0_callback[i]();
        }
    }
}

/*exit2*/
void exti2_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti2_callback[i++] = (exit_callback)f;
    }
}

void exti2_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti2_callback[i]) {
            exti2_callback[i]();
        }
    }
}

/*exit4*/
void exti3_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti3_callback[i++] = (exit_callback)f;
    }
}

void exti3_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti3_callback[i]) {
            exti3_callback[i]();
        }
    }
}

/*exit4*/
void exti4_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti4_callback[i++] = (exit_callback)f;
    }
}

void exti4_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti4_callback[i]) {
            exti4_callback[i]();
        }
    }
}
/*exit5*/
void exti5_irq_register(void *f)
{
    static uint8_t i = 0;
    uint8_t j = 0;

    for (j = 0; j < i; j++) {
        if (exti5_callback[j] == (exit_callback)f) return;
    }
    if (i < 5) {
        exti5_callback[i++] = (exit_callback)f;
    }
}

void exti5_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti5_callback[i]) {
            exti5_callback[i]();
        }
    }
}

/*exit8*/
void exti8_irq_register(void *f)
{
    static uint8_t i = 0;
    uint8_t j = 0;

    for (j = 0; j < i; j++) {
        if (exti8_callback[j] == (exit_callback)f) return;
    }
    if (i < 5) {
        exti8_callback[i++] = (exit_callback)f;
    }
}

void exti8_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti8_callback[i]) {
            exti8_callback[i]();
        }
    }
}

/*exit10*/
void exti10_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti10_callback[i++] = (exit_callback)f;
    }
}

void exti10_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti10_callback[i]) {
            exti10_callback[i]();
        }
    }
}

/*exit11*/
void exti11_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti11_callback[i++] = (exit_callback)f;
    }
}

void exti11_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti11_callback[i]) {
            exti11_callback[i]();
        }
    }
}

/*exit12*/
void exti12_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti12_callback[i++] = (exit_callback)f;
    }
}

void exti12_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti12_callback[i]) {
            exti12_callback[i]();
        }
    }
}

/*exit10*/
void exti13_irq_register(void *f)
{
    static uint8_t i = 0;
    if (i < 5) {
        exti13_callback[i++] = (exit_callback)f;
    }
}

void exti13_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti13_callback[i]) {
            exti13_callback[i]();
        }
    }
}

/*exit15*/
void exti15_irq_register(void *f)
{
    static uint8_t i = 0;
    uint8_t j = 0;

    for (j = 0; j < 5; j++) {
        if (exti15_callback[j] == (exit_callback)f) return;
    }
    if (i < 5) {
        exti15_callback[i++] = (exit_callback)f;
    }
}

void exti15_all_callback(void)
{
    uint8_t i;
    for (i = 0; i < 5; i++) {
        if (exti15_callback[i]) {
            exti15_callback[i]();
        }
    }
}
