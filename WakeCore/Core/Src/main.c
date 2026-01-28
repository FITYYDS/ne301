/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os2.h"
#include "crc.h"
#include "dma.h"
#include "iwdg.h"
#include "usart.h"
#include "rtc.h"
#include "spi.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif
PUTCHAR_PROTOTYPE
{
	HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);
	return ch;
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void Flash_OPT_Check(void)
{
  uint8_t wflag = 0;
  FLASH_OBProgramInitTypeDef obConfig = {0};

  // printf("Flash OPT Check...\r\n");
  HAL_FLASH_Unlock();
  HAL_FLASH_OB_Unlock();

  HAL_FLASHEx_OBGetConfig(&obConfig);
  // Check if IWDG is disabled in STOP and STDBY modes
  if (obConfig.USERConfig & FLASH_OPTR_IWDG_STDBY) {
    printf("IWDG_STDBY enable\r\n");
    obConfig.USERConfig &= ~FLASH_OPTR_IWDG_STDBY;
    wflag = 1;
  }
  if (obConfig.USERConfig & FLASH_OPTR_IWDG_STOP) {
    printf("IWDG_STOP enable\r\n");
    obConfig.USERConfig &= ~FLASH_OPTR_IWDG_STOP;
    wflag = 1;
  }
  if (wflag) {
    obConfig.OptionType = OPTIONBYTE_USER;
    if (HAL_FLASHEx_OBProgram(&obConfig) == HAL_OK) {
      if (HAL_FLASH_OB_Launch() != HAL_OK) HAL_NVIC_SystemReset();
    } else {
      printf("Flash OPT program failed!\r\n");
    }
  }
  // Check if RDP level is 0
  // if (obConfig.RDPLevel == OB_RDP_LEVEL_0) {
  //   printf("RDP Level is 0\r\n");
  //   obConfig.RDPLevel = OB_RDP_LEVEL_1;
  //   wflag = 1;
  // }
  // if (wflag) {
  //   obConfig.OptionType = OPTIONBYTE_RDP;
  //   if (HAL_FLASHEx_OBProgram(&obConfig) == HAL_OK) {
  //     if (HAL_FLASH_OB_Launch() != HAL_OK) HAL_NVIC_SystemReset();
  //   } else {
  //     printf("Flash OPT program failed!\r\n");
  //   }
  // }
  HAL_FLASH_OB_Lock();
  HAL_FLASH_Lock();
}
/**
 * @brief TIM6 microsecond delay initialization
 * @note System clock 56MHz, TIM6 clock 56MHz
 */
void TIM6_Delay_Init(void)
{
    // 1. Enable TIM6 clock
    __HAL_RCC_TIM6_CLK_ENABLE();
    
    // 2. Configure TIM6
    TIM6->PSC = 15;         // Prescaler = 15, counter clock = 1MHz
    TIM6->ARR = 0xFFFF;     // Auto-reload value, 16-bit maximum
    TIM6->CR1 = 0;          // Up-counting mode
    TIM6->EGR |= TIM_EGR_UG; // Generate update event, reset counter
    
    // 3. Start timer
    TIM6->CR1 |= TIM_CR1_CEN;
}
/**
 * @brief TIM6 microsecond delay function
 * @param us: Delay time in microseconds (1-65535)
 */
void delay_us(uint16_t us)
{
    uint16_t start = TIM6->CNT;
    uint16_t delay_ticks = us;
    
    // Handle counter overflow case
    if((0xFFFF - start) >= delay_ticks)
    {
        // No overflow case
        while((TIM6->CNT - start) < delay_ticks);
    }
    else
    {
        // Overflow case
        delay_ticks -= (0xFFFF - start);
        while(TIM6->CNT < delay_ticks);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  // MX_CRC_Init();
  MX_LPUART2_UART_Init();
  MX_USART1_UART_Init();
  // MX_SPI3_Init();
  MX_RTC_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
  Flash_OPT_Check();
  TIM6_Delay_Init();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Call init function for freertos objects (in app_freertos.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  printf("Error_Handler\r\n");
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  printf("[%s : %ld]assert failed!\r\n", file, line);
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
