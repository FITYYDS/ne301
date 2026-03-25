/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2s.c
  * @brief   This file provides code for the configuration
  *          of the I2S instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "i2s.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

I2S_HandleTypeDef hi2s6;

/* SPI6 and I2S6 share the same peripheral and DMA channels.
 * DMA handles are owned and initialised by spi.c; re-linked here. */
extern DMA_HandleTypeDef handle_HPDMA1_Channel9;  /* SPI6_RX / I2S6_RX */
extern DMA_HandleTypeDef handle_HPDMA1_Channel8;  /* SPI6_TX / I2S6_TX */

/* I2S6 init function */
void MX_I2S6_Init(void)
{

  /* USER CODE BEGIN I2S6_Init 0 */

  /* USER CODE END I2S6_Init 0 */

  /* USER CODE BEGIN I2S6_Init 1 */

  /* USER CODE END I2S6_Init 1 */
  /* STM32 is I2S master (drives BCLK, WS). NAU88C10 is slave (CLKIOEN=0).
   * Codec uses its 12.288 MHz crystal for internal MCLK. */
  hi2s6.Instance = SPI6;
  hi2s6.Init.Mode = I2S_MODE_MASTER_FULLDUPLEX;
  hi2s6.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s6.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s6.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s6.Init.AudioFreq = I2S_AUDIOFREQ_16K;   /* informational only in slave mode */
  hi2s6.Init.CPOL = I2S_CPOL_LOW;
  hi2s6.Init.FirstBit = I2S_FIRSTBIT_MSB;
  hi2s6.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
  hi2s6.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
  hi2s6.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_DISABLE;
  if (HAL_I2S_Init(&hi2s6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S6_Init 2 */

  /* USER CODE END I2S6_Init 2 */

}

void HAL_I2S_MspInit(I2S_HandleTypeDef* i2sHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(i2sHandle->Instance==SPI6)
  {
  /* USER CODE BEGIN SPI6_MspInit 0 */

  /* USER CODE END SPI6_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI6;
    PeriphClkInitStruct.Spi6ClockSelection = RCC_SPI6CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* I2S6 clock enable */
    __HAL_RCC_SPI6_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2S6 GPIO Configuration
    PA5     ------> I2S6_CK
    PA0     ------> I2S6_WS
    PA7     ------> I2S6_SDO
    PB4(NJTRST)     ------> I2S6_SDI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_SPI6;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI6;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_SPI6;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    DMA_IsolationConfigTypeDef IsolationConfig;

    /* I2S6 DMA Init (re-initialises the shared SPI6 DMA channels for I2S use) */
    /* HPDMA1_Channel9 – SPI6_RX / I2S6_RX */
    handle_HPDMA1_Channel9.Instance                  = HPDMA1_Channel9;
    handle_HPDMA1_Channel9.Init.Request              = HPDMA1_REQUEST_SPI6_RX;
    handle_HPDMA1_Channel9.Init.BlkHWRequest         = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel9.Init.Direction            = DMA_PERIPH_TO_MEMORY;
    handle_HPDMA1_Channel9.Init.SrcInc               = DMA_SINC_FIXED;
    handle_HPDMA1_Channel9.Init.DestInc              = DMA_DINC_INCREMENTED;
    handle_HPDMA1_Channel9.Init.SrcDataWidth         = DMA_SRC_DATAWIDTH_HALFWORD;
    handle_HPDMA1_Channel9.Init.DestDataWidth        = DMA_DEST_DATAWIDTH_HALFWORD;
    handle_HPDMA1_Channel9.Init.Priority             = DMA_LOW_PRIORITY_LOW_WEIGHT;
    handle_HPDMA1_Channel9.Init.SrcBurstLength       = 1;
    handle_HPDMA1_Channel9.Init.DestBurstLength      = 1;
    handle_HPDMA1_Channel9.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT1 | DMA_DEST_ALLOCATED_PORT0;
    handle_HPDMA1_Channel9.Init.TransferEventMode    = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel9.Init.Mode                 = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel9) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(i2sHandle, hdmarx, handle_HPDMA1_Channel9);

    /* Restore TrustZone security attributes lost by HAL_DMA_Init */
    IsolationConfig.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfig.StaticCid    = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel9, &IsolationConfig) != HAL_OK)
    {
      Error_Handler();
    }
    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel9,
            DMA_CHANNEL_SEC | DMA_CHANNEL_PRIV |
            DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      Error_Handler();
    }

    /* HPDMA1_Channel8 – SPI6_TX / I2S6_TX */
    handle_HPDMA1_Channel8.Instance                  = HPDMA1_Channel8;
    handle_HPDMA1_Channel8.Init.Request              = HPDMA1_REQUEST_SPI6_TX;
    handle_HPDMA1_Channel8.Init.BlkHWRequest         = DMA_BREQ_SINGLE_BURST;
    handle_HPDMA1_Channel8.Init.Direction            = DMA_MEMORY_TO_PERIPH;
    handle_HPDMA1_Channel8.Init.SrcInc               = DMA_SINC_INCREMENTED;
    handle_HPDMA1_Channel8.Init.DestInc              = DMA_DINC_FIXED;
    handle_HPDMA1_Channel8.Init.SrcDataWidth         = DMA_SRC_DATAWIDTH_HALFWORD;
    handle_HPDMA1_Channel8.Init.DestDataWidth        = DMA_DEST_DATAWIDTH_HALFWORD;
    handle_HPDMA1_Channel8.Init.Priority             = DMA_LOW_PRIORITY_LOW_WEIGHT;
    handle_HPDMA1_Channel8.Init.SrcBurstLength       = 1;
    handle_HPDMA1_Channel8.Init.DestBurstLength      = 1;
    handle_HPDMA1_Channel8.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1;
    handle_HPDMA1_Channel8.Init.TransferEventMode    = DMA_TCEM_BLOCK_TRANSFER;
    handle_HPDMA1_Channel8.Init.Mode                 = DMA_NORMAL;
    if (HAL_DMA_Init(&handle_HPDMA1_Channel8) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(i2sHandle, hdmatx, handle_HPDMA1_Channel8);

    /* Restore TrustZone security attributes lost by HAL_DMA_Init */
    IsolationConfig.CidFiltering = DMA_ISOLATION_ON;
    IsolationConfig.StaticCid    = DMA_CHANNEL_STATIC_CID_1;
    if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel8, &IsolationConfig) != HAL_OK)
    {
      Error_Handler();
    }
    if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel8,
            DMA_CHANNEL_SEC | DMA_CHANNEL_PRIV |
            DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC) != HAL_OK)
    {
      Error_Handler();
    }

  /* USER CODE BEGIN SPI6_MspInit 1 */

  /* USER CODE END SPI6_MspInit 1 */
  }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef* i2sHandle)
{

  if(i2sHandle->Instance==SPI6)
  {
  /* USER CODE BEGIN SPI6_MspDeInit 0 */

  /* USER CODE END SPI6_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI6_CLK_DISABLE();

    /**I2S6 GPIO Configuration
    PA5     ------> I2S6_CK
    PA0     ------> I2S6_WS
    PA7     ------> I2S6_SDO
    PB4(NJTRST)     ------> I2S6_SDI
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_5|GPIO_PIN_0|GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_4);

    /* I2S6 DMA DeInit */
    HAL_DMA_DeInit(i2sHandle->hdmarx);
    HAL_DMA_DeInit(i2sHandle->hdmatx);
  /* USER CODE BEGIN SPI6_MspDeInit 1 */

  /* USER CODE END SPI6_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
