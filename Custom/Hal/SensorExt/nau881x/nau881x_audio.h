/**
 * @file nau881x_audio.h
 * @brief NAU881x I2S audio playback and recording layer.
 *
 * Sits on top of nau881x_dev (codec register control) and the STM32 I2S6
 * peripheral (hi2s6 / DMA).  All transfers use DMA in normal (non-circular)
 * mode so each call blocks until the DMA interrupt fires or a timeout expires.
 *
 * Typical flow
 * ------------
 * Record:
 *   nau881x_audio_record_start(buf, samples)  -- configure codec + start DMA RX
 *   nau881x_audio_record_wait(timeout_ms)     -- block until done
 *   nau881x_audio_record_stop()               -- mute/power-down ADC path
 *
 * Playback:
 *   nau881x_audio_play_start(buf, samples)    -- configure codec + start DMA TX
 *   nau881x_audio_play_wait(timeout_ms)       -- block until done
 *   nau881x_audio_play_stop()                 -- mute/power-down DAC path
 */

#ifndef NAU881X_AUDIO_H
#define NAU881X_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Return codes */
#define NAU881X_AUDIO_OK            (0)
#define NAU881X_AUDIO_ERR_NOT_INIT  (-1)
#define NAU881X_AUDIO_ERR_BUSY      (-2)
#define NAU881X_AUDIO_ERR_PARAM     (-3)
#define NAU881X_AUDIO_ERR_TIMEOUT   (-4)
#define NAU881X_AUDIO_ERR_HAL       (-5)

/**
 * @brief Initialize the I2S peripheral and codec for audio use.
 *
 * Calls MX_I2S6_Init() and nau881x_dev_init() (caller must have already
 * called i2c_driver_init(I2C_PORT_1)).  Safe to call multiple times.
 *
 * @return NAU881X_AUDIO_OK on success, negative on error.
 */
int nau881x_audio_init(void);

/**
 * @brief Deinitialize I2S and codec.
 */
void nau881x_audio_deinit(void);

/* ------------------------------------------------------------------ */
/* Playback                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Configure the codec DAC path and start a DMA TX transfer.
 *
 * @param buf      16-bit PCM samples (must be accessible by DMA).
 * @param samples  Number of 16-bit samples (not bytes).
 * @param vol_db   Speaker volume in dB (-57 to +5).
 * @return NAU881X_AUDIO_OK on success, negative on error.
 */
int nau881x_audio_play_start(const int16_t *buf, uint32_t samples, int8_t vol_db);

/**
 * @brief Wait for the current playback DMA transfer to complete.
 *
 * @param timeout_ms  Maximum wait time in milliseconds (0 = poll once).
 * @return NAU881X_AUDIO_OK when done, NAU881X_AUDIO_ERR_TIMEOUT if expired.
 */
int nau881x_audio_play_wait(uint32_t timeout_ms);

/**
 * @brief Stop playback immediately and mute the speaker.
 */
void nau881x_audio_play_stop(void);

/**
 * @brief Returns non-zero while a playback transfer is in progress.
 */
int nau881x_audio_play_is_busy(void);

/**
 * @brief Request playback to stop (checked by play loop between chunks).
 */
void nau881x_audio_play_request_stop(void);

/**
 * @brief Clear stop request (call at start of new play).
 */
void nau881x_audio_play_clear_stop_request(void);

/**
 * @brief Returns non-zero if stop was requested.
 */
int nau881x_audio_play_is_stop_requested(void);

/* ------------------------------------------------------------------ */
/* Recording                                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Configure the codec ADC path and start a DMA RX transfer.
 *
 * @param buf      Destination buffer for 16-bit PCM samples (DMA-accessible).
 * @param samples  Number of 16-bit samples to capture.
 * @param pga_gain PGA gain register value (0..63, ~0.75 dB/step, 0 = -12 dB).
 * @return NAU881X_AUDIO_OK on success, negative on error.
 */
int nau881x_audio_record_start(int16_t *buf, uint32_t samples, uint8_t pga_gain);

/**
 * @brief Wait for the current recording DMA transfer to complete.
 *
 * @param timeout_ms  Maximum wait time in milliseconds.
 * @return NAU881X_AUDIO_OK when done, NAU881X_AUDIO_ERR_TIMEOUT if expired.
 */
int nau881x_audio_record_wait(uint32_t timeout_ms);

/**
 * @brief Stop recording immediately and power down the ADC path.
 */
void nau881x_audio_record_stop(void);

/**
 * @brief Returns non-zero while a recording transfer is in progress.
 */
int nau881x_audio_record_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* NAU881X_AUDIO_H */
