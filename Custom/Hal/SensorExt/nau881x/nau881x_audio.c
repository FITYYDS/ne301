/**
 * @file nau881x_audio.c
 * @brief NAU881x I2S audio playback and recording layer.
 *
 * STM32 I2S6 is master (drives BCLK, WS). NAU88C10 is slave (CLKIOEN=0).
 * Codec uses 12.288 MHz crystal for internal MCLK.
 *
 * Playback: HAL_I2S_Transmit_DMA (I2S_MODE_MASTER_TX).
 * Recording: HAL_I2S_Receive_DMA (I2S_MODE_MASTER_RX).
 */

#include <stdbool.h>

#include "nau881x_audio.h"
#include "nau881x_dev.h"
#include "nau881x.h"
#include "nau881x_regs.h"
#include "i2s.h"
#include "debug.h"
#include "aicam_types.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "stm32n6xx_hal.h"

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */

static bool s_audio_initialized = false;

/* Semaphores released by HAL TX/RX complete callbacks */
static osSemaphoreId_t s_tx_sem = NULL;
static osSemaphoreId_t s_rx_sem = NULL;

static volatile bool s_play_busy         = false;
static volatile bool s_play_stop_request = false;
static volatile bool s_record_busy       = false;
static bool         s_i2s_tx_session     = false;  /* I2S inited for TX, skip DeInit/Init between play chunks */
static bool         s_i2s_rx_session     = false;  /* I2S inited for RX, skip DeInit/Init between record chunks */

/* RX buffer info saved for post-DMA cache invalidation */
static int16_t  *s_rx_buf       = NULL;
static uint32_t  s_rx_buf_bytes = 0;

/* ------------------------------------------------------------------ */
/* HAL I2S callbacks                                                   */
/* ------------------------------------------------------------------ */

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI6) return;
    s_play_busy = false;
    if (s_tx_sem != NULL) osSemaphoreRelease(s_tx_sem);
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI6) return;
    s_record_busy = false;
    if (s_rx_sem != NULL) osSemaphoreRelease(s_rx_sem);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s->Instance != SPI6) return;
    LOG_DRV_ERROR("nau881x_audio: I2S error 0x%08lX state=%d\r\n",
                  hi2s->ErrorCode, (int)hi2s->State);
    s_play_busy   = false;
    s_record_busy = false;
    if (s_tx_sem != NULL) osSemaphoreRelease(s_tx_sem);
    if (s_rx_sem != NULL) osSemaphoreRelease(s_rx_sem);
}

/* ------------------------------------------------------------------ */
/* Init / deinit                                                       */
/* ------------------------------------------------------------------ */

int nau881x_audio_init(void)
{
    if (s_audio_initialized) {
        /* If codec handle is still valid, nothing to do */
        if (nau881x_dev_get_handle() != NULL) {
            return NAU881X_AUDIO_OK;
        }
        /* Codec was deinitialized externally — re-init it */
        s_audio_initialized = false;
    }

    MX_I2S6_Init();

    int ret = nau881x_dev_init();
    if (ret != AICAM_OK && ret != AICAM_ERROR_ALREADY_INITIALIZED) {
        LOG_DRV_ERROR("nau881x_audio: codec init failed %d\r\n", ret);
        return NAU881X_AUDIO_ERR_NOT_INIT;
    }

    s_tx_sem = osSemaphoreNew(1, 0, NULL);
    s_rx_sem = osSemaphoreNew(1, 0, NULL);
    if (s_tx_sem == NULL || s_rx_sem == NULL) {
        LOG_DRV_ERROR("nau881x_audio: semaphore creation failed\r\n");
        return NAU881X_AUDIO_ERR_NOT_INIT;
    }

    /* Configure codec as I2S slave (CLKIOEN=0). STM32 drives BCLK and WS.
     * Codec uses 12.288 MHz crystal for internal MCLK. SMPLR=3 for 16 kHz. */
    NAU881x_t *h = nau881x_dev_get_handle();
    if (h != NULL) {
        uint8_t rev = 0;
        NAU881x_Get_SiliconRevision(h, &rev);
        LOG_DRV_DEBUG("nau881x_audio: silicon_rev=0x%02X\r\n", (unsigned)rev);

        NAU881x_Set_AudioInterfaceFormat(h, NAU881X_AUDIO_IFACE_FMT_I2S, NAU881X_AUDIO_IFACE_WL_16BITS);
        NAU881x_Set_Clock(h, 0, NAU881X_BCLKDIV_8, NAU881X_MCLKDIV_3, NAU881X_CLKSEL_MCLK);  /* slave */
        NAU881x_Set_SampleRate(h, 3);  /* 16 kHz */
        NAU881x_Set_DAC_SoftMute(h, 1);
        NAU881x_Set_DAC_Enable(h, 1);

        osDelay(50);
    }

    s_audio_initialized = true;
    LOG_DRV_DEBUG("nau881x_audio: init OK\r\n");
    return NAU881X_AUDIO_OK;
}

void nau881x_audio_deinit(void)
{
    if (!s_audio_initialized) return;

    nau881x_audio_play_stop();
    nau881x_audio_record_stop();
    HAL_I2S_DeInit(&hi2s6);

    if (s_tx_sem) { osSemaphoreDelete(s_tx_sem); s_tx_sem = NULL; }
    if (s_rx_sem) { osSemaphoreDelete(s_rx_sem); s_rx_sem = NULL; }

    nau881x_dev_deinit();
    s_audio_initialized = false;
}

/* ------------------------------------------------------------------ */
/* Playback                                                            */
/* ------------------------------------------------------------------ */

int nau881x_audio_play_start(const int16_t *buf, uint32_t samples, int8_t vol_db)
{
    if (!s_audio_initialized) return NAU881X_AUDIO_ERR_NOT_INIT;
    if (buf == NULL || samples == 0) return NAU881X_AUDIO_ERR_PARAM;
    if (s_play_busy) return NAU881X_AUDIO_ERR_BUSY;

    NAU881x_t *h = nau881x_dev_get_handle();
    if (h == NULL) return NAU881X_AUDIO_ERR_NOT_INIT;

    /* First chunk of play session: full codec + I2S init. Skip for subsequent chunks to avoid glitches. */
    if (!s_i2s_tx_session) {
        NAU881x_Set_DAC_Gain(h, 0xFF);
        NAU881x_Set_DAC_SoftMute(h, 0);
        NAU881x_Set_Speaker_Source(h, NAU881X_OUTPUT_FROM_DAC);
        NAU881x_Set_Speaker_Volume_db(h, vol_db);
        NAU881x_Set_Speaker_Mute(h, 0);
        /* VDDSPK=5V: enable SPK boost for full output swing (OUTPUT_CTRL bit 2) */
        NAU881x_Set_Speaker_Boost(h, 1);
        NAU881x_Set_Mono_Boost(h, 1);  /* VDDSPK 5V: MOUTBST=1 per Power Up table */
        NAU881x_Set_Output_Enable(h, NAU881X_OUTPUT_SPK);

        HAL_I2S_DeInit(&hi2s6);
        hi2s6.Init.Mode = I2S_MODE_MASTER_TX;
        if (HAL_I2S_Init(&hi2s6) != HAL_OK) return NAU881X_AUDIO_ERR_HAL;
        WRITE_REG(hi2s6.Instance->IFCR, 0x0FF8U);
        s_i2s_tx_session = true;
    }

    osSemaphoreAcquire(s_tx_sem, 0);
    s_play_busy = true;

    /* Flush D-Cache so DMA reads the data CPU just wrote to the TX buffer.
     * Address and size must be 32-byte aligned for Cortex-M7. */
    uint32_t buf_bytes = samples * sizeof(int16_t);
    uint32_t start_addr = (uint32_t)buf & ~0x1FU;
    uint32_t end_addr = ((uint32_t)buf + buf_bytes + 31U) & ~0x1FU;
    SCB_CleanDCache_by_Addr((void *)start_addr, (int32_t)(end_addr - start_addr));

    HAL_StatusTypeDef st = HAL_I2S_Transmit_DMA(
        &hi2s6,
        (const uint16_t *)(uintptr_t)buf,
        (uint16_t)samples);

    if (st != HAL_OK) {
        s_play_busy = false;
        LOG_DRV_ERROR("nau881x_audio: HAL_I2S_Transmit_DMA failed %d\r\n", (int)st);
        return NAU881X_AUDIO_ERR_HAL;
    }

    return NAU881X_AUDIO_OK;
}

int nau881x_audio_play_wait(uint32_t timeout_ms)
{
    /* Always try to acquire the semaphore: TxCplt may have already fired
     * (e.g. for very short buffers) and released it before we get here. */
    osStatus_t s = osSemaphoreAcquire(s_tx_sem, timeout_ms);
    if (s == osOK) return NAU881X_AUDIO_OK;
    return NAU881X_AUDIO_ERR_TIMEOUT;
}

void nau881x_audio_play_stop(void)
{
    if (!s_audio_initialized) return;
    HAL_I2S_DMAStop(&hi2s6);
    s_play_busy = false;
    s_i2s_tx_session = false;

    NAU881x_t *h = nau881x_dev_get_handle();
    if (h == NULL) return;
    NAU881x_Set_Speaker_Mute(h, 1);
    NAU881x_Set_DAC_SoftMute(h, 1);
    NAU881x_Set_Output_Enable(h, NAU881X_OUTPUT_NONE);
}

int nau881x_audio_play_is_busy(void)
{
    return s_play_busy ? 1 : 0;
}

void nau881x_audio_play_request_stop(void)
{
    s_play_stop_request = true;
}

void nau881x_audio_play_clear_stop_request(void)
{
    s_play_stop_request = false;
}

int nau881x_audio_play_is_stop_requested(void)
{
    return s_play_stop_request ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Recording                                                           */
/* ------------------------------------------------------------------ */

int nau881x_audio_record_start(int16_t *buf, uint32_t samples, uint8_t pga_gain)
{
    if (!s_audio_initialized) return NAU881X_AUDIO_ERR_NOT_INIT;
    if (buf == NULL || samples == 0) return NAU881X_AUDIO_ERR_PARAM;
    if (s_record_busy) return NAU881X_AUDIO_ERR_BUSY;

    NAU881x_t *h = nau881x_dev_get_handle();
    if (h == NULL) return NAU881X_AUDIO_ERR_NOT_INIT;

    /* First chunk: full codec + I2S init. Skip for subsequent chunks to avoid glitches.
     * Single-ended MIC+ per DS 6.1.1.1: PMICPGA=1, NMICPGA=0 (MIC- gets internal 30k to VREF).
     * PGA + PGABST + PMICBSTGAIN for max analog gain when needed. */
    if (!s_i2s_rx_session) {
        NAU881x_Set_MicBias_Enable(h, 1);
        NAU881x_Set_PGA_Input(h, NAU881X_INPUT_MICP | NAU881X_INPUT_MICN);  /* single-ended MIC+, MIC- not connected (DS 6.1.1.1) */
        NAU881x_Set_PGA_Enable(h, 1);
        NAU881x_Set_PGA_Gain(h, pga_gain);  /* fixed gain when ALC off */
        NAU881x_Set_PGA_Mute(h, 0);
        NAU881x_Set_PGA_Boost(h, 1);        /* PGABST +20 dB on PGA output path (DS 6.1.2) */
        NAU881x_Set_Boost_Enable(h, 1);
        NAU881x_Set_Boost_Volume(h, NAU881X_INPUT_MICP, 0x03);  /* PMICBSTGAIN=7: +6 dB on MIC+ path (max) */
        NAU881x_Set_ADC_Enable(h, 1);
        NAU881x_Set_ADC_HighPassFilter(h, 1, NAU881X_HPF_MODE_AUDIO, 0x01);  /* HPF on: remove DC/low-freq */

        HAL_I2S_DeInit(&hi2s6);
        hi2s6.Init.Mode = I2S_MODE_MASTER_RX;
        if (HAL_I2S_Init(&hi2s6) != HAL_OK) return NAU881X_AUDIO_ERR_HAL;
        WRITE_REG(hi2s6.Instance->IFCR, 0x0FF8U);
        s_i2s_rx_session = true;
    }

    osSemaphoreAcquire(s_rx_sem, 0);
    s_record_busy   = true;
    s_rx_buf        = buf;
    s_rx_buf_bytes  = samples * sizeof(int16_t);

    SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)buf & ~0x1FU),
                                 (int32_t)(s_rx_buf_bytes + 32U));

    HAL_StatusTypeDef st = HAL_I2S_Receive_DMA(&hi2s6, (uint16_t *)buf, (uint16_t)samples);

    if (st != HAL_OK) {
        s_record_busy = false;
        s_i2s_rx_session = false;
        NAU881x_Set_ADC_Enable(h, 0);
        NAU881x_Set_Boost_Enable(h, 0);
        NAU881x_Set_PGA_Enable(h, 0);
        NAU881x_Set_MicBias_Enable(h, 0);
        LOG_DRV_ERROR("nau881x_audio: HAL_I2S_Receive_DMA failed %d\r\n", (int)st);
        return NAU881X_AUDIO_ERR_HAL;
    }

    return NAU881X_AUDIO_OK;
}

int nau881x_audio_record_wait(uint32_t timeout_ms)
{
    osStatus_t s = osSemaphoreAcquire(s_rx_sem, timeout_ms);

    /* Invalidate D-Cache AFTER DMA has finished writing to RAM. */
    if (s_rx_buf != NULL && s_rx_buf_bytes > 0) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)((uint32_t)s_rx_buf & ~0x1FU),
                                     (int32_t)(s_rx_buf_bytes + 32U));
    }

    if (s == osOK) return NAU881X_AUDIO_OK;
    return NAU881X_AUDIO_ERR_TIMEOUT;
}

void nau881x_audio_record_stop(void)
{
    if (!s_audio_initialized) return;
    HAL_I2S_DMAStop(&hi2s6);
    s_record_busy = false;
    s_i2s_rx_session = false;

    NAU881x_t *h = nau881x_dev_get_handle();
    if (h == NULL) return;
    NAU881x_Set_ADC_Enable(h, 0);
    NAU881x_Set_Boost_Enable(h, 0);
    NAU881x_Set_PGA_Mute(h, 1);
    NAU881x_Set_PGA_Enable(h, 0);
    NAU881x_Set_MicBias_Enable(h, 0);
}

int nau881x_audio_record_is_busy(void)
{
    return s_record_busy ? 1 : 0;
}
