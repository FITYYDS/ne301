/**
 * @file nau881x_cmd.c
 * @brief Debug CLI commands for the NAU881x audio codec.
 *
 * Usage:  nau881x <subcmd> [args]
 *
 * Commands
 * --------
 *   nau881x init                       - Init codec on I2C port 1 (addr 0x1A)
 *   nau881x deinit                     - Deinit codec and release I2C
 *   nau881x rev                        - Read silicon revision
 *
 *   -- Input / PGA --
 *   nau881x pga_input <none|micp|micn|aux>  - Select PGA input source
 *   nau881x pga_gain  <0..63>               - Set PGA gain register value
 *   nau881x pga_gain_db <-12.0..35.0>       - Set PGA gain in dB
 *   nau881x pga_mute  <0|1>                 - Mute/unmute PGA
 *   nau881x pga_boost <0|1>                 - Enable/disable PGA boost (+20 dB)
 *   nau881x pga_en    <0|1>                 - Power PGA on/off
 *   nau881x micbias_en <0|1>                - Enable/disable MIC bias
 *
 *   -- ADC --
 *   nau881x adc_en    <0|1>                 - Enable/disable ADC
 *   nau881x adc_gain  <0..255>              - Set ADC digital gain (0xFF = 0 dB)
 *   nau881x adc_hpf   <en> <mode> <freq>    - High-pass filter (en=0/1, mode=0/1, freq=0..7)
 *   nau881x boost_en  <0|1>                 - Enable/disable ADC boost stage
 *
 *   -- ALC --
 *   nau881x alc_en    <0|1>                 - Enable/disable ALC
 *   nau881x alc_gain  <min> <max>           - Set ALC min/max gain (0..7 each)
 *   nau881x alc_target <0..15>              - Set ALC target level
 *   nau881x alc_mode  <normal|limiter>      - Set ALC mode
 *   nau881x ng_en     <0|1>                 - Enable/disable noise gate
 *   nau881x ng_thresh <0..7>                - Set noise gate threshold
 *
 *   -- DAC --
 *   nau881x dac_en    <0|1>                 - Enable/disable DAC
 *   nau881x dac_gain  <0..255>              - Set DAC digital gain (0xFF = 0 dB)
 *   nau881x dac_mute  <0|1>                 - Soft-mute DAC
 *   nau881x dac_passthrough <0|1>           - ADC-to-DAC digital passthrough
 *
 *   -- Speaker output --
 *   nau881x spk_en    <0|1>                 - Enable/disable speaker output
 *   nau881x spk_vol   <0..63>               - Set speaker volume (register value)
 *   nau881x spk_vol_db <-57..5>             - Set speaker volume in dB
 *   nau881x spk_mute  <0|1>                 - Mute/unmute speaker
 *   nau881x spk_src   <dac|bypass>          - Speaker mixer source
 *   nau881x bypass    <on|off>              - MIC analog path to speaker (live monitor, same as codec.c)
 *
 *   -- Clock --
 *   nau881x clock <master> <bclkdiv> <mclkdiv> <src>
 *                                           - Configure clock (master=0/1,
 *                                             bclkdiv=0..5, mclkdiv=0..7,
 *                                             src=0=MCLK/1=PLL)
 *   nau881x fmt <fmt> <wl>                  - Audio interface format
 *                                             fmt: rj|lj|i2s|pcma|pcmb
 *                                             wl:  16|20|24|32|8
 *
 *   -- Audio (I2S) --
 *   nau881x record <path> [duration_sec] [pga_gain]  - Record to file
 *   nau881x play <path> [vol_db]                     - Play from file
 *   nau881x play_mute [duration_sec] [vol_db]        - Play silence (test circuit noise)
 *   nau881x play_scale [vol_db]                      - Play C major scale (Do~Do)
 *
 *   -- Register dump --
 *   nau881x dump                            - Dump cached register values
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "../i2c_driver/i2c_driver.h"
#include "nau881x_dev.h"
#include "nau881x_audio.h"
#include "nau881x.h"
#include "nau881x_regs.h"
#include "generic_cmdline.h"
#include "generic_file.h"
#include "mem.h"
#include "cmsis_os2.h"
#include "i2s.h"

#define NAU881X_SAMPLE_RATE 16000
#define NAU881X_REC_CHUNK_SAMPLES 16000
#define NAU881X_PLAY_CHUNK_SAMPLES 8000   /* Smaller than rec: stereo=32KB/chunk, avoids DMA err on PSRAM */
#define NAU881X_WAV_HEADER_SIZE 44
#define NAU881X_PLAY_PATH_MAX 96

/* Playback state for background thread */
static volatile bool s_play_thread_running = false;
static osThreadId_t s_play_thread_id = NULL;  /* Must osThreadTerminate to free stack+TCB (CMSIS-RTOS2/ThreadX) */
static char s_play_path_buf[NAU881X_PLAY_PATH_MAX];
static int8_t s_play_vol_db;
static uint32_t s_play_mute_duration_sec;
static const osThreadAttr_t s_play_thread_attr = {
    .name = "nau881x_play",
    .stack_size = 4096U,  /* File I/O + HAL/codec call depth can overflow 2KB */
    .priority = osPriorityNormal,
};

/* Expand mono to stereo for I2S: each sample -> [L, R] = [s, s] */
static void mono_to_stereo(int16_t *buf, uint32_t mono_samples)
{
    for (int32_t i = (int32_t)mono_samples - 1; i >= 0; i--) {
        int16_t s = buf[i];
        buf[2 * i]     = s;
        buf[2 * i + 1] = s;
    }
}

/* Extract mono from I2S stereo buffer [L0,R0,L1,R1,...] -> [L0,L1,...] (in-place) */
static void stereo_to_mono(int16_t *buf, uint32_t mono_samples)
{
    for (uint32_t i = 0; i < mono_samples; i++) {
        buf[i] = buf[2 * i];
    }
}

/* Forward declaration for use in nau881x_play_thread */
static int nau881x_wav_parse_header(void *fd, uint32_t *out_data_bytes, uint16_t *out_channels);

/* Playback thread: runs play logic, checks stop request between chunks */
static void nau881x_play_thread(void *arg)
{
    (void)arg;
    const char *path = s_play_path_buf;
    int8_t vol_db = s_play_vol_db;

    nau881x_audio_play_clear_stop_request();

    void *fd = file_fopen(path, "rb");
    if (!fd) {
        LOG_SIMPLE("nau881x: cannot open %s for read", path);
        goto done;
    }

    size_t stereo_buf_size = 2 * NAU881X_PLAY_CHUNK_SAMPLES * sizeof(int16_t);
    size_t mono_read_size = NAU881X_PLAY_CHUNK_SAMPLES * sizeof(int16_t);
    int16_t *buf[2];
    /* Prefer MEM_FAST (internal RAM) to avoid PSRAM/DMA noise when playing */
    buf[0] = (int16_t *)hal_mem_alloc_aligned(stereo_buf_size, 32, MEM_FAST);
    buf[1] = (int16_t *)hal_mem_alloc_aligned(stereo_buf_size, 32, MEM_FAST);
    if (!buf[0] || !buf[1]) {
        if (buf[0]) hal_mem_free(buf[0]);
        if (buf[1]) hal_mem_free(buf[1]);
        buf[0] = (int16_t *)hal_mem_alloc_aligned(stereo_buf_size, 32, MEM_LARGE);
        buf[1] = (int16_t *)hal_mem_alloc_aligned(stereo_buf_size, 32, MEM_LARGE);
    }
    if (!buf[0] || !buf[1]) {
        LOG_SIMPLE("nau881x: play malloc failed");
        if (buf[0]) hal_mem_free(buf[0]);
        if (buf[1]) hal_mem_free(buf[1]);
        file_fclose(fd);
        goto done;
    }

    uint32_t data_bytes;
    uint16_t channels;
    if (nau881x_wav_parse_header(fd, &data_bytes, &channels) != 0) {
        LOG_SIMPLE("nau881x: invalid WAV file");
        hal_mem_free(buf[0]);
        hal_mem_free(buf[1]);
        file_fclose(fd);
        goto done;
    }

    int is_stereo = (channels == 2);
    size_t read_size = is_stereo ? stereo_buf_size : mono_read_size;
    uint32_t total_played = 0;
    int ret = 0;
    int cur = 0;

    int n = file_fread(fd, buf[0], read_size);
    if (n <= 0) {
        hal_mem_free(buf[0]);
        hal_mem_free(buf[1]);
        file_fclose(fd);
        goto done;
    }
    uint32_t stereo_words = (uint32_t)n / sizeof(int16_t);
    if (stereo_words == 0) {
        hal_mem_free(buf[0]);
        hal_mem_free(buf[1]);
        file_fclose(fd);
        goto done;
    }
    if (!is_stereo) {
        mono_to_stereo(buf[0], stereo_words);
        stereo_words *= 2;
    }

    if (nau881x_audio_play_start(buf[0], stereo_words, vol_db) != NAU881X_AUDIO_OK) {
        LOG_SIMPLE("nau881x: play_start failed");
        hal_mem_free(buf[0]);
        hal_mem_free(buf[1]);
        file_fclose(fd);
        goto done;
    }
    total_played += stereo_words / 2;
    cur = 1;

    uint32_t last_chunk_stereo_words = stereo_words;
    for (;;) {
        n = file_fread(fd, buf[cur], read_size);
        uint32_t timeout_ms = (last_chunk_stereo_words / 32U) + 2000U;
        if (nau881x_audio_play_wait(timeout_ms) != NAU881X_AUDIO_OK) {
            nau881x_audio_play_stop();
            LOG_SIMPLE("nau881x: play timeout");
            ret = -1;
            break;
        }
        if (nau881x_audio_play_is_stop_requested()) {
            nau881x_audio_play_stop();
            LOG_SIMPLE("nau881x: play stopped by user");
            break;
        }
        if (n <= 0) break;
        stereo_words = (uint32_t)n / sizeof(int16_t);
        if (stereo_words == 0) break;
        if (!is_stereo) {
            mono_to_stereo(buf[cur], stereo_words);
            stereo_words *= 2;
        }

        if (nau881x_audio_play_start(buf[cur], stereo_words, vol_db) != NAU881X_AUDIO_OK) {
            LOG_SIMPLE("nau881x: play_start failed");
            ret = -1;
            break;
        }
        total_played += stereo_words / 2;
        last_chunk_stereo_words = stereo_words;
        cur = 1 - cur;
    }

    nau881x_audio_play_stop();
    file_fclose(fd);
    hal_mem_free(buf[0]);
    hal_mem_free(buf[1]);
    if (ret == 0) {
        LOG_SIMPLE("nau881x: play done, %lu samples", (unsigned long)total_played);
    }

done:
    s_play_thread_running = false;
    osThreadExit();  /* Never returns; ensures thread termination */
}

/* Play mute thread: plays silence to test circuit noise floor (no signal from file) */
static void nau881x_play_mute_thread(void *arg)
{
    (void)arg;
    uint32_t duration_sec = s_play_mute_duration_sec;
    int8_t vol_db = s_play_vol_db;

    nau881x_audio_play_clear_stop_request();

    size_t stereo_buf_size = 2 * NAU881X_PLAY_CHUNK_SAMPLES * sizeof(int16_t);
    int16_t *buf = (int16_t *)hal_mem_alloc_aligned(stereo_buf_size, 32, MEM_FAST);
    if (buf == NULL) buf = (int16_t *)hal_mem_alloc_aligned(stereo_buf_size, 32, MEM_LARGE);
    if (buf == NULL) {
        LOG_SIMPLE("nau881x: play_mute malloc failed");
        s_play_thread_running = false;
        osThreadExit();
    }
    memset(buf, 0, stereo_buf_size);

    uint32_t stereo_samples = 2 * NAU881X_PLAY_CHUNK_SAMPLES;
    uint32_t elapsed_ms = 0;
    uint32_t duration_ms = duration_sec * 1000U;
    uint32_t chunk_ms = (NAU881X_PLAY_CHUNK_SAMPLES * 1000U) / NAU881X_SAMPLE_RATE;

    LOG_SIMPLE("nau881x: play_mute %lu s (vol=%d dB), listen for hiss; play_stop to stop",
               (unsigned long)duration_sec, (int)vol_db);

    for (;;) {
        if (nau881x_audio_play_start(buf, stereo_samples, vol_db) != NAU881X_AUDIO_OK) {
            LOG_SIMPLE("nau881x: play_mute play_start failed");
            break;
        }
        uint32_t tmo = chunk_ms + 500U;
        if (nau881x_audio_play_wait(tmo) != NAU881X_AUDIO_OK) {
            nau881x_audio_play_stop();
            LOG_SIMPLE("nau881x: play_mute timeout");
            break;
        }
        elapsed_ms += chunk_ms;
        if (nau881x_audio_play_is_stop_requested() || (duration_sec > 0 && elapsed_ms >= duration_ms)) {
            nau881x_audio_play_stop();
            break;
        }
    }

    hal_mem_free(buf);
    s_play_thread_running = false;
    LOG_SIMPLE("nau881x: play_mute done");
    osThreadExit();
}

/* Write WAV header (44 bytes): mono, 16 kHz, 16-bit PCM */
static void nau881x_wav_write_header(void *fd, uint32_t pcm_bytes)
{
    uint8_t hdr[NAU881X_WAV_HEADER_SIZE];
    uint32_t riff_size = 36 + pcm_bytes;
    uint32_t byte_rate = NAU881X_SAMPLE_RATE * 1 * 16 / 8;

    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 4, &riff_size, 4);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    *(uint32_t *)(hdr + 16) = 16;
    *(uint16_t *)(hdr + 20) = 1;
    *(uint16_t *)(hdr + 22) = 1;
    *(uint32_t *)(hdr + 24) = NAU881X_SAMPLE_RATE;
    memcpy(hdr + 28, &byte_rate, 4);
    *(uint16_t *)(hdr + 32) = 2;
    *(uint16_t *)(hdr + 34) = 16;
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &pcm_bytes, 4);
    file_fwrite(fd, hdr, NAU881X_WAV_HEADER_SIZE);
}

/* Parse WAV header, return 0 on success.
 * out_data_bytes: PCM data size in bytes
 * out_channels: 1=mono, 2=stereo */
static int nau881x_wav_parse_header(void *fd, uint32_t *out_data_bytes, uint16_t *out_channels)
{
    uint8_t hdr[NAU881X_WAV_HEADER_SIZE];
    if (file_fread(fd, hdr, NAU881X_WAV_HEADER_SIZE) != NAU881X_WAV_HEADER_SIZE) {
        return -1;
    }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0 ||
        memcmp(hdr + 12, "fmt ", 4) != 0 || memcmp(hdr + 36, "data", 4) != 0) {
        return -1;
    }
    uint32_t sr = *(uint32_t *)(hdr + 24);
    uint16_t ch = *(uint16_t *)(hdr + 22);
    uint16_t bps = *(uint16_t *)(hdr + 34);
    if (sr != NAU881X_SAMPLE_RATE || bps != 16) {
        LOG_SIMPLE("nau881x: WAV must be 16 kHz 16-bit (got %lu Hz %u bit)",
                   (unsigned long)sr, (unsigned)bps);
        return -1;
    }
    if (ch != 1 && ch != 2) {
        LOG_SIMPLE("nau881x: WAV must be mono or stereo (got %u ch)", (unsigned)ch);
        return -1;
    }
    *out_data_bytes = *(uint32_t *)(hdr + 40);
    *out_channels = ch;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void print_usage(void)
{
    LOG_SIMPLE("Usage: nau881x <subcmd> [args]");
    LOG_SIMPLE("  init                       - Init codec (I2C1, addr 0x1A)");
    LOG_SIMPLE("  deinit                     - Deinit codec and release I2C");
    LOG_SIMPLE("  rev                        - Read silicon revision");
    LOG_SIMPLE("  -- Input/PGA --");
    LOG_SIMPLE("  pga_input <none|micp|micn|aux>");
    LOG_SIMPLE("  pga_gain  <0..63>");
    LOG_SIMPLE("  pga_gain_db <dB>           - e.g. 0.0, 12.0, -6.0");
    LOG_SIMPLE("  pga_mute  <0|1>");
    LOG_SIMPLE("  pga_boost <0|1>            - +20 dB boost");
    LOG_SIMPLE("  pga_en    <0|1>");
    LOG_SIMPLE("  micbias_en <0|1>");
    LOG_SIMPLE("  -- ADC --");
    LOG_SIMPLE("  adc_en    <0|1>");
    LOG_SIMPLE("  adc_gain  <0..255>         - 0xFF = 0 dB");
    LOG_SIMPLE("  adc_hpf   <en> <mode> <freq>  - mode: 0=audio 1=app, freq: 0..7");
    LOG_SIMPLE("  boost_en  <0|1>");
    LOG_SIMPLE("  -- ALC --");
    LOG_SIMPLE("  alc_en    <0|1>");
    LOG_SIMPLE("  alc_gain  <min> <max>      - 0..7 each");
    LOG_SIMPLE("  alc_target <0..15>");
    LOG_SIMPLE("  alc_mode  <normal|limiter>");
    LOG_SIMPLE("  ng_en     <0|1>");
    LOG_SIMPLE("  ng_thresh <0..7>");
    LOG_SIMPLE("  -- DAC --");
    LOG_SIMPLE("  dac_en    <0|1>");
    LOG_SIMPLE("  dac_gain  <0..255>         - 0xFF = 0 dB");
    LOG_SIMPLE("  dac_mute  <0|1>");
    LOG_SIMPLE("  dac_passthrough <0|1>");
    LOG_SIMPLE("  -- Speaker --");
    LOG_SIMPLE("  spk_en    <0|1>");
    LOG_SIMPLE("  spk_vol   <0..63>");
    LOG_SIMPLE("  spk_vol_db <-57..5>");
    LOG_SIMPLE("  spk_mute  <0|1>");
    LOG_SIMPLE("  spk_src   <dac|bypass>");
    LOG_SIMPLE("  bypass    <on|off>       - MIC analog path to speaker (live monitor, same as codec.c)");
    LOG_SIMPLE("  -- Clock / Interface --");
    LOG_SIMPLE("  clock <master> <bclkdiv> <mclkdiv> <src>");
    LOG_SIMPLE("  fmt   <rj|lj|i2s|pcma|pcmb> <16|20|24|32|8>");
    LOG_SIMPLE("  -- Audio (I2S) --");
    LOG_SIMPLE("  audio_init                 - Init I2S + codec for audio");
    LOG_SIMPLE("  audio_deinit               - Deinit I2S + codec");
    LOG_SIMPLE("  record <path> [duration_sec] [pga_gain]");
    LOG_SIMPLE("                             - Record MIC to WAV file (16 kHz, mono, 16-bit)");
    LOG_SIMPLE("                               path: output file path (.wav)");
    LOG_SIMPLE("                               duration_sec: seconds (default 1)");
    LOG_SIMPLE("                               pga_gain: 0..63 (default 16)");
    LOG_SIMPLE("  play <path> [vol_db]       - Play WAV file (background thread)");
    LOG_SIMPLE("                               path: 16 kHz, mono/stereo, 16-bit");
    LOG_SIMPLE("                               vol_db: speaker volume in dB (default 0)");
    LOG_SIMPLE("  play_stop                  - Stop playback");
    LOG_SIMPLE("  play_mute [sec] [vol_db]   - Play silence (sec=0: until play_stop)");
    LOG_SIMPLE("  play_scale [vol_db]        - Play Do Re Mi Fa Sol La Si Do (C4~C5)");
    LOG_SIMPLE("  -- Debug --");
    LOG_SIMPLE("  dump                       - Dump cached register values");
}

/** Return handle or print error and return NULL. */
static NAU881x_t *get_handle_or_err(void)
{
    NAU881x_t *h = nau881x_dev_get_handle();
    if (h == NULL) {
        LOG_SIMPLE("nau881x: not initialized (run 'nau881x init' first)");
    }
    return h;
}

static const char *status_str(nau881x_status_t s)
{
    switch (s) {
        case NAU881X_STATUS_OK:      return "OK";
        case NAU881X_STATUS_ERROR:   return "ERROR";
        case NAU881X_STATUS_INVALID: return "INVALID";
        default:                     return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Command handler                                                     */
/* ------------------------------------------------------------------ */

static int nau881x_cmd(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return -1;
    }

    const char *sub = argv[1];

    /* ---- init ---- */
    if (strcmp(sub, "init") == 0) {
        int ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("nau881x: i2c_driver_init failed %d", ret);
            return ret;
        }
        ret = nau881x_dev_init();
        if (ret == 0) {
            LOG_SIMPLE("nau881x: init OK");
        } else {
            LOG_SIMPLE("nau881x: init failed %d", ret);
            i2c_driver_deinit(I2C_PORT_1);
        }
        return ret;
    }

    /* ---- deinit ---- */
    if (strcmp(sub, "deinit") == 0) {
        nau881x_audio_deinit();
        nau881x_dev_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("nau881x: deinit done (codec + I2C port released)");
        return 0;
    }

    /* ---- rev ---- */
    if (strcmp(sub, "rev") == 0) {
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t rev = 0;
        nau881x_status_t s = NAU881x_Get_SiliconRevision(h, &rev);
        if (s == NAU881X_STATUS_OK) {
            LOG_SIMPLE("nau881x: silicon revision = 0x%02X", rev);
        } else {
            LOG_SIMPLE("nau881x: rev read failed (%s)", status_str(s));
        }
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- pga_input ---- */
    if (strcmp(sub, "pga_input") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x pga_input <none|micp|micn|aux>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        nau881x_input_t inp;
        if      (strcmp(argv[2], "none") == 0) inp = NAU881X_INPUT_NONE;
        else if (strcmp(argv[2], "micp") == 0) inp = NAU881X_INPUT_MICP;
        else if (strcmp(argv[2], "micn") == 0) inp = NAU881X_INPUT_MICN;
        else if (strcmp(argv[2], "aux")  == 0) inp = NAU8814_INPUT_AUX;
        else { LOG_SIMPLE("nau881x: unknown input '%s'", argv[2]); return -1; }
        nau881x_status_t s = NAU881x_Set_PGA_Input(h, inp);
        LOG_SIMPLE("nau881x: pga_input=%s -> %s", argv[2], status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- pga_gain ---- */
    if (strcmp(sub, "pga_gain") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x pga_gain <0..63>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t val = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_PGA_Gain(h, val);
        LOG_SIMPLE("nau881x: pga_gain=%u -> %s", (unsigned)val, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- pga_gain_db ---- */
    if (strcmp(sub, "pga_gain_db") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x pga_gain_db <dB>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        float db = (float)atof(argv[2]);
        nau881x_status_t s = NAU881x_Set_PGA_Gain_db(h, db);
        LOG_SIMPLE("nau881x: pga_gain_db=%.2f -> %s", (double)db, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- pga_mute ---- */
    if (strcmp(sub, "pga_mute") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x pga_mute <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_PGA_Mute(h, v);
        LOG_SIMPLE("nau881x: pga_mute=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- pga_boost ---- */
    if (strcmp(sub, "pga_boost") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x pga_boost <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_PGA_Boost(h, v);
        LOG_SIMPLE("nau881x: pga_boost=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- pga_en ---- */
    if (strcmp(sub, "pga_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x pga_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_PGA_Enable(h, v);
        LOG_SIMPLE("nau881x: pga_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- micbias_en ---- */
    if (strcmp(sub, "micbias_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x micbias_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_MicBias_Enable(h, v);
        LOG_SIMPLE("nau881x: micbias_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- adc_en ---- */
    if (strcmp(sub, "adc_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x adc_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ADC_Enable(h, v);
        LOG_SIMPLE("nau881x: adc_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- adc_gain ---- */
    if (strcmp(sub, "adc_gain") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x adc_gain <0..255>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ADC_Gain(h, v);
        LOG_SIMPLE("nau881x: adc_gain=0x%02X -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- adc_hpf ---- */
    if (strcmp(sub, "adc_hpf") == 0) {
        if (argc < 5) { LOG_SIMPLE("nau881x adc_hpf <en> <mode> <freq>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t en   = (uint8_t)strtoul(argv[2], NULL, 0);
        uint8_t mode = (uint8_t)strtoul(argv[3], NULL, 0);
        uint8_t freq = (uint8_t)strtoul(argv[4], NULL, 0);
        nau881x_hpf_mode_t hpf_mode = mode ? NAU881X_HPF_MODE_APP : NAU881X_HPF_MODE_AUDIO;
        nau881x_status_t s = NAU881x_Set_ADC_HighPassFilter(h, en, hpf_mode, freq);
        LOG_SIMPLE("nau881x: adc_hpf en=%u mode=%u freq=%u -> %s",
                   (unsigned)en, (unsigned)mode, (unsigned)freq, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- boost_en ---- */
    if (strcmp(sub, "boost_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x boost_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_Boost_Enable(h, v);
        LOG_SIMPLE("nau881x: boost_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- alc_en ---- */
    if (strcmp(sub, "alc_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x alc_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ALC_Enable(h, v);
        LOG_SIMPLE("nau881x: alc_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- alc_gain ---- */
    if (strcmp(sub, "alc_gain") == 0) {
        if (argc < 4) { LOG_SIMPLE("nau881x alc_gain <min 0..7> <max 0..7>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t mn = (uint8_t)strtoul(argv[2], NULL, 0);
        uint8_t mx = (uint8_t)strtoul(argv[3], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ALC_Gain(h, mn, mx);
        LOG_SIMPLE("nau881x: alc_gain min=%u max=%u -> %s", (unsigned)mn, (unsigned)mx, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- alc_target ---- */
    if (strcmp(sub, "alc_target") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x alc_target <0..15>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ALC_TargetLevel(h, v);
        LOG_SIMPLE("nau881x: alc_target=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- alc_mode ---- */
    if (strcmp(sub, "alc_mode") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x alc_mode <normal|limiter>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        nau881x_alc_mode_t mode;
        if      (strcmp(argv[2], "normal")  == 0) mode = NAU881X_ALC_MODE_NORMAL;
        else if (strcmp(argv[2], "limiter") == 0) mode = NAU881X_ALC_MODE_LIMITER;
        else { LOG_SIMPLE("nau881x: alc_mode must be 'normal' or 'limiter'"); return -1; }
        nau881x_status_t s = NAU881x_Set_ALC_Mode(h, mode);
        LOG_SIMPLE("nau881x: alc_mode=%s -> %s", argv[2], status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- ng_en ---- */
    if (strcmp(sub, "ng_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x ng_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ALC_NoiseGate_Enable(h, v);
        LOG_SIMPLE("nau881x: ng_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- ng_thresh ---- */
    if (strcmp(sub, "ng_thresh") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x ng_thresh <0..7>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ALC_NoiseGate_Threshold(h, v);
        LOG_SIMPLE("nau881x: ng_thresh=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- dac_en ---- */
    if (strcmp(sub, "dac_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x dac_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_DAC_Enable(h, v);
        LOG_SIMPLE("nau881x: dac_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- dac_gain ---- */
    if (strcmp(sub, "dac_gain") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x dac_gain <0..255>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_DAC_Gain(h, v);
        LOG_SIMPLE("nau881x: dac_gain=0x%02X -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- dac_mute ---- */
    if (strcmp(sub, "dac_mute") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x dac_mute <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_DAC_SoftMute(h, v);
        LOG_SIMPLE("nau881x: dac_mute=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- dac_passthrough ---- */
    if (strcmp(sub, "dac_passthrough") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x dac_passthrough <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_ADC_DAC_Passthrough(h, v);
        LOG_SIMPLE("nau881x: dac_passthrough=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- spk_en ---- */
    if (strcmp(sub, "spk_en") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x spk_en <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_output_t out = v ? NAU881X_OUTPUT_SPK : NAU881X_OUTPUT_NONE;
        nau881x_status_t s = NAU881x_Set_Output_Enable(h, out);
        LOG_SIMPLE("nau881x: spk_en=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- spk_vol ---- */
    if (strcmp(sub, "spk_vol") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x spk_vol <0..63>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_Speaker_Volume(h, v);
        LOG_SIMPLE("nau881x: spk_vol=%u (reg) -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- spk_vol_db ---- */
    if (strcmp(sub, "spk_vol_db") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x spk_vol_db <-57..5>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        int8_t db = (int8_t)atoi(argv[2]);
        nau881x_status_t s = NAU881x_Set_Speaker_Volume_db(h, db);
        LOG_SIMPLE("nau881x: spk_vol_db=%d dB -> %s", (int)db, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- spk_mute ---- */
    if (strcmp(sub, "spk_mute") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x spk_mute <0|1>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t v = (uint8_t)strtoul(argv[2], NULL, 0);
        nau881x_status_t s = NAU881x_Set_Speaker_Mute(h, v);
        LOG_SIMPLE("nau881x: spk_mute=%u -> %s", (unsigned)v, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- spk_src ---- */
    if (strcmp(sub, "spk_src") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x spk_src <dac|bypass>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        nau881x_output_source_t src;
        if      (strcmp(argv[2], "dac")    == 0) src = NAU881X_OUTPUT_FROM_DAC;
        else if (strcmp(argv[2], "bypass") == 0) src = NAU881X_OUTPUT_FROM_BYPASS;
        else { LOG_SIMPLE("nau881x: spk_src must be 'dac' or 'bypass'"); return -1; }
        nau881x_status_t s = NAU881x_Set_Speaker_Source(h, src);
        LOG_SIMPLE("nau881x: spk_src=%s -> %s", argv[2], status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- bypass ---- (same as codec.c NAU881x_Enable/Disable_Mic_Bypass_To_SPK) */
    if (strcmp(sub, "bypass") == 0) {
        if (argc < 3) { LOG_SIMPLE("nau881x bypass <on|off>"); return -1; }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        if (strcmp(argv[2], "on") == 0) {
            NAU881x_Set_PGA_Input(h, NAU881X_INPUT_MICP);
            NAU881x_Set_PGA_Enable(h, 1);
            NAU881x_Set_PGA_Gain(h, 0x3F);   /* 35.25 dB max */
            NAU881x_Set_Boost_Enable(h, 1);
            NAU881x_Set_Boost_Volume(h, NAU881X_INPUT_MICP, 0x07);
            NAU881x_Set_MicBias_Enable(h, 1);
            NAU881x_Set_Speaker_Source(h, NAU881X_OUTPUT_FROM_BYPASS);
            NAU881x_Set_Output_Enable(h, NAU881X_OUTPUT_SPK);
            NAU881x_Set_Speaker_Volume(h, 0x3F);
            NAU881x_Set_Speaker_Mute(h, 0);
            LOG_SIMPLE("MIC bypass to SPK enabled.");
        } else if (strcmp(argv[2], "off") == 0) {
            /* Same sequence as codec.c NAU881x_Disable_Mic_Bypass_To_SPK */
            NAU881x_Set_PGA_Input(h, NAU881X_INPUT_MICP);
            NAU881x_Set_PGA_Enable(h, 1);
            NAU881x_Set_PGA_Gain(h, 0x3F);
            NAU881x_Set_Boost_Enable(h, 1);
            NAU881x_Set_Boost_Volume(h, NAU881X_INPUT_MICP, 0x07);
            NAU881x_Set_MicBias_Enable(h, 1);
            NAU881x_Set_Speaker_Source(h, NAU881X_OUTPUT_FROM_BYPASS);
            NAU881x_Set_Output_Enable(h, NAU881X_OUTPUT_SPK);
            NAU881x_Set_Speaker_Volume(h, 0x3F);
            NAU881x_Set_Speaker_Mute(h, 1);
            LOG_SIMPLE("MIC bypass to SPK disabled.");
        } else {
            LOG_SIMPLE("nau881x bypass <on|off>");
            return -1;
        }
        return 0;
    }

    /* ---- clock ---- */
    if (strcmp(sub, "clock") == 0) {
        if (argc < 6) {
            LOG_SIMPLE("nau881x clock <master 0|1> <bclkdiv 0..5> <mclkdiv 0..7> <src 0=MCLK|1=PLL>");
            return -1;
        }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        uint8_t master   = (uint8_t)strtoul(argv[2], NULL, 0);
        uint8_t bclkdiv  = (uint8_t)strtoul(argv[3], NULL, 0);
        uint8_t mclkdiv  = (uint8_t)strtoul(argv[4], NULL, 0);
        uint8_t src      = (uint8_t)strtoul(argv[5], NULL, 0);
        nau881x_status_t s = NAU881x_Set_Clock(h, master,
                                               (nau881x_bclkdiv_t)bclkdiv,
                                               (nau881x_mclkdiv_t)mclkdiv,
                                               (nau881x_clksel_t)src);
        LOG_SIMPLE("nau881x: clock master=%u bclkdiv=%u mclkdiv=%u src=%u -> %s",
                   (unsigned)master, (unsigned)bclkdiv, (unsigned)mclkdiv, (unsigned)src,
                   status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ---- fmt ---- */
    if (strcmp(sub, "fmt") == 0) {
        if (argc < 4) {
            LOG_SIMPLE("nau881x fmt <rj|lj|i2s|pcma|pcmb> <16|20|24|32|8>");
            return -1;
        }
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        nau881x_audio_iface_fmt_t fmt;
        if      (strcmp(argv[2], "rj")   == 0) fmt = NAU881X_AUDIO_IFACE_FMT_RIGHT_JUSTIFIED;
        else if (strcmp(argv[2], "lj")   == 0) fmt = NAU881X_AUDIO_IFACE_FMT_LEFT_JUSTIFIED;
        else if (strcmp(argv[2], "i2s")  == 0) fmt = NAU881X_AUDIO_IFACE_FMT_I2S;
        else if (strcmp(argv[2], "pcma") == 0) fmt = NAU881X_AUDIO_IFACE_FMT_PCM_A;
        else if (strcmp(argv[2], "pcmb") == 0) fmt = NAU881X_AUDIO_IFACE_FMT_PCM_B;
        else { LOG_SIMPLE("nau881x: unknown fmt '%s'", argv[2]); return -1; }
        nau881x_audio_iface_wl_t wl;
        int wl_bits = atoi(argv[3]);
        switch (wl_bits) {
            case 16: wl = NAU881X_AUDIO_IFACE_WL_16BITS; break;
            case 20: wl = NAU881X_AUDIO_IFACE_WL_20BITS; break;
            case 24: wl = NAU881X_AUDIO_IFACE_WL_24BITS; break;
            case 32: wl = NAU881X_AUDIO_IFACE_WL_32BITS; break;
            case 8:  wl = NAU881X_AUDIO_IFACE_WL_8BITS;  break;
            default: LOG_SIMPLE("nau881x: word length must be 8/16/20/24/32"); return -1;
        }
        nau881x_status_t s = NAU881x_Set_AudioInterfaceFormat(h, fmt, wl);
        LOG_SIMPLE("nau881x: fmt=%s wl=%d -> %s", argv[2], wl_bits, status_str(s));
        return (s == NAU881X_STATUS_OK) ? 0 : -1;
    }

    /* ------------------------------------------------------------------ */
    /* Audio (I2S) commands                                               */
    /* ------------------------------------------------------------------ */

    /* ---- audio_init ---- */
    if (strcmp(sub, "audio_init") == 0) {
        int ret = i2c_driver_init(I2C_PORT_1);
        if (ret != 0) {
            LOG_SIMPLE("nau881x: i2c_driver_init failed %d", ret);
            return ret;
        }
        ret = nau881x_audio_init();
        if (ret == NAU881X_AUDIO_OK) {
            LOG_SIMPLE("nau881x: audio_init OK (I2S6 + codec ready)");
        } else {
            LOG_SIMPLE("nau881x: audio_init failed %d", ret);
            i2c_driver_deinit(I2C_PORT_1);
        }
        return ret;
    }

    /* ---- audio_deinit ---- */
    if (strcmp(sub, "audio_deinit") == 0) {
        nau881x_audio_deinit();
        i2c_driver_deinit(I2C_PORT_1);
        LOG_SIMPLE("nau881x: audio_deinit done");
        return 0;
    }

    /* ---- record ---- */
    if (strcmp(sub, "record") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("nau881x record <path> [duration_sec] [pga_gain]");
            return -1;
        }
        const char *path = argv[2];
        uint32_t duration_sec = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 1;
        uint8_t  pga_gain     = (argc >= 5) ? (uint8_t)strtoul(argv[4], NULL, 0)  : 16;  /* default 16 */

        if (duration_sec == 0 || duration_sec > 3600) {
            LOG_SIMPLE("nau881x: duration_sec must be 1..3600");
            return -1;
        }

        void *fd = file_fopen(path, "wb");
        if (!fd) {
            LOG_SIMPLE("nau881x: cannot open %s for write", path);
            return -1;
        }

        size_t buf_size = NAU881X_REC_CHUNK_SAMPLES * sizeof(int16_t);
        int16_t *buf[2];
        buf[0] = (int16_t *)hal_mem_alloc_large(buf_size);
        buf[1] = (int16_t *)hal_mem_alloc_large(buf_size);
        if (!buf[0] || !buf[1]) {
            LOG_SIMPLE("nau881x: record malloc failed");
            if (buf[0]) hal_mem_free(buf[0]);
            if (buf[1]) hal_mem_free(buf[1]);
            file_fclose(fd);
            return -1;
        }

        uint32_t total_samples = duration_sec * NAU881X_SAMPLE_RATE;
        uint32_t pcm_bytes = total_samples * sizeof(int16_t);
        uint32_t written = 0;
        int ret = 0;
        int cur = 0;

        nau881x_wav_write_header(fd, pcm_bytes);

        LOG_SIMPLE("nau881x: recording %lu s to %s (pga_gain=%u)", (unsigned long)duration_sec, path, (unsigned)pga_gain);

        /* I2S RX is stereo (L,R); we extract left channel. Max 8000 mono per 16K-word buffer. */
        uint32_t chunk = total_samples;
        if (chunk > NAU881X_REC_CHUNK_SAMPLES / 2) chunk = NAU881X_REC_CHUNK_SAMPLES / 2;

        if (nau881x_audio_record_start(buf[cur], chunk * 2, pga_gain) != NAU881X_AUDIO_OK) {
            LOG_SIMPLE("nau881x: record_start failed");
            ret = -1;
            goto record_cleanup;
        }
        uint32_t timeout_ms = (chunk / 16U) + 2000U;
        if (nau881x_audio_record_wait(timeout_ms) != NAU881X_AUDIO_OK) {
            nau881x_audio_record_stop();
            LOG_SIMPLE("nau881x: record timeout");
            ret = -1;
            goto record_cleanup;
        }
        stereo_to_mono(buf[cur], chunk);
        written += chunk;
        total_samples -= chunk;

        while (total_samples > 0 && ret == 0) {
            uint32_t next_chunk = total_samples;
            if (next_chunk > NAU881X_REC_CHUNK_SAMPLES / 2) next_chunk = NAU881X_REC_CHUNK_SAMPLES / 2;

            /* Start next record immediately (no stop between chunks) */
            if (nau881x_audio_record_start(buf[1 - cur], next_chunk * 2, pga_gain) != NAU881X_AUDIO_OK) {
                LOG_SIMPLE("nau881x: record_start failed");
                ret = -1;
                break;
            }
            /* Write previous chunk (already processed) while next is recording */
            size_t to_write = chunk * sizeof(int16_t);
            int n = file_fwrite(fd, buf[cur], to_write);
            if (n != (int)to_write) {
                nau881x_audio_record_stop();
                LOG_SIMPLE("nau881x: write failed");
                ret = -1;
                break;
            }
            timeout_ms = (next_chunk / 16U) + 2000U;
            if (nau881x_audio_record_wait(timeout_ms) != NAU881X_AUDIO_OK) {
                nau881x_audio_record_stop();
                LOG_SIMPLE("nau881x: record timeout");
                ret = -1;
                break;
            }
            stereo_to_mono(buf[1 - cur], next_chunk);
            written += next_chunk;
            total_samples -= next_chunk;
            chunk = next_chunk;
            cur = 1 - cur;
        }

        if (ret == 0 && written > 0) {
            /* Write last chunk (if we had more than one iteration, cur was swapped) */
            size_t to_write = chunk * sizeof(int16_t);
            if (file_fwrite(fd, buf[cur], to_write) != (int)to_write) {
                LOG_SIMPLE("nau881x: write failed");
                ret = -1;
            }
        }

record_cleanup:
        nau881x_audio_record_stop();
        file_fclose(fd);
        hal_mem_free(buf[0]);
        hal_mem_free(buf[1]);
        if (ret == 0) {
            LOG_SIMPLE("nau881x: record done, %lu samples", (unsigned long)written);
        }
        return ret;
    }

    /* ---- play_stop ---- */
    if (strcmp(sub, "play_stop") == 0) {
        nau881x_audio_play_request_stop();
        nau881x_audio_play_stop();
        LOG_SIMPLE("nau881x: play stop requested");
        return 0;
    }

    /* ---- play_mute ---- */
    if (strcmp(sub, "play_mute") == 0) {
        if (s_play_thread_running) {
            LOG_SIMPLE("nau881x: play already in progress, use play_stop first");
            return -1;
        }
        if (s_play_thread_id != NULL) {
            osThreadTerminate(s_play_thread_id);
            s_play_thread_id = NULL;
        }
        s_play_mute_duration_sec = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 10U;
        s_play_vol_db = (argc >= 4) ? (int8_t)atoi(argv[3]) : 0;
        s_play_thread_running = true;
        s_play_thread_id = osThreadNew(nau881x_play_mute_thread, NULL, &s_play_thread_attr);
        if (s_play_thread_id == NULL) {
            s_play_thread_running = false;
            LOG_SIMPLE("nau881x: play_mute thread create failed");
            return -1;
        }
        return 0;
    }

    /* ---- play ---- */
    if (strcmp(sub, "play") == 0) {
        if (argc < 3) {
            LOG_SIMPLE("nau881x play <path> [vol_db]");
            return -1;
        }
        if (s_play_thread_running) {
            LOG_SIMPLE("nau881x: play already in progress, use play_stop first");
            return -1;
        }
        if (s_play_thread_id != NULL) {
            osThreadTerminate(s_play_thread_id);
            s_play_thread_id = NULL;
        }
        const char *path = argv[2];
        int8_t vol_db = (argc >= 4) ? (int8_t)atoi(argv[3]) : 0;
        size_t path_len = strlen(path);
        if (path_len >= NAU881X_PLAY_PATH_MAX) {
            LOG_SIMPLE("nau881x: path too long");
            return -1;
        }
        memcpy(s_play_path_buf, path, path_len + 1);
        s_play_vol_db = vol_db;
        s_play_thread_running = true;
        s_play_thread_id = osThreadNew(nau881x_play_thread, NULL, &s_play_thread_attr);
        if (s_play_thread_id == NULL) {
            s_play_thread_running = false;
            LOG_SIMPLE("nau881x: play thread create failed");
            return -1;
        }
        LOG_SIMPLE("nau881x: playing %s (vol=%d dB), use play_stop to stop", path, (int)vol_db);
        return 0;
    }

    /* ---- play_scale ---- */
    if (strcmp(sub, "play_scale") == 0) {
        int8_t vol_db = (argc >= 3) ? (int8_t)atoi(argv[2]) : 0;

        static const int16_t sine256[256] = {
               0,   736,  1472,  2206,  2938,  3668,  4395,  5119,
            5837,  6551,  7259,  7961,  8655,  9341, 10018, 10686,
           11343, 11988, 12622, 13243, 13851, 14444, 15024, 15588,
           16135, 16666, 17179, 17674, 18150, 18606, 19043, 19459,
           19854, 20228, 20580, 20910, 21216, 21499, 21758, 21993,
           22204, 22390, 22551, 22686, 22797, 22882, 22941, 22975,
           22983, 22965, 22921, 22852, 22756, 22635, 22488, 22316,
           22118, 21896, 21648, 21376, 21079, 20758, 20413, 20044,
           19652, 19237, 18799, 18339, 17857, 17353, 16828, 16282,
           15716, 15130, 14525, 13901, 13259, 12600, 11923, 11230,
           10521,  9797,  9059,  8307,  7542,  6765,  5977,  5178,
            4369,  3551,  2724,  1890,  1050,   205,  -643, -1493,
           -2344, -3194, -4042, -4887, -5728, -6565, -7395, -8219,
           -9035, -9842,-10639,-11426,-12201,-12963,-13712,-14447,
          -15167,-15871,-16558,-17228,-17879,-18511,-19123,-19714,
          -20284,-20832,-21357,-21859,-22337,-22790,-23218,-23620,
          -23996,-24345,-24667,-24961,-25227,-25464,-25673,-25852,
          -26002,-26122,-26212,-26272,-26302,-26301,-26270,-26208,
          -26116,-25994,-25841,-25658,-25445,-25202,-24929,-24627,
          -24295,-23935,-23546,-23129,-22684,-22211,-21711,-21184,
          -20631,-20052,-19447,-18818,-18164,-17486,-16785,-16061,
          -15315,-14548,-13760,-12952,-12125,-11279,-10416, -9536,
           -8640, -7729, -6804, -5866, -4916, -3955, -2984, -2004,
           -1016,   -21,   976,  1972,  2966,  3956,  4941,  5921,
            6893,  7858,  8814,  9760, 10695, 11618, 12528, 13424,
           14305, 15170, 16018, 16848, 17659, 18449, 19219, 19967,
           20692, 21393, 22070, 22721, 23346, 23944, 24514, 25055,
           25566, 26047, 26497, 26915, 27301, 27654, 27973, 28258,
           28508, 28723, 28903, 29046, 29153, 29223, 29257, 29254,
           29213, 29136, 29021, 28870, 28682, 28457, 28196, 27899,
           27566, 27197, 26793, 26354, 25881, 25373, 24832, 24257,
           23650, 23011, 22340, 21638, 20906, 20145, 19355, 18537,
        };

        static const uint32_t note_phase_inc[] = {
            274491U, 308096U, 345748U, 366299U, 411206U, 461373U, 518043U, 548982U,
        };
        static const char * const note_names[] = {
            "Do(C4)","Re(D4)","Mi(E4)","Fa(F4)","Sol(G4)","La(A4)","Si(B4)","Do(C5)"
        };

        const uint32_t NOTE_SAMPLES    = 8000U;
        const uint32_t SILENCE_SAMPLES = 1600U;
        const uint32_t BUF_SAMPLES     = NOTE_SAMPLES + SILENCE_SAMPLES;

        /* Stereo buffer for I2S (2 words per mono sample). 32-byte aligned for DMA. */
        int16_t *buf = (int16_t *)hal_mem_alloc_aligned(2 * BUF_SAMPLES * sizeof(int16_t), 32, MEM_LARGE);
        if (buf == NULL) {
            LOG_SIMPLE("nau881x: play_scale: malloc failed");
            return -1;
        }

        int overall_ret = 0;
        for (int note = 0; note < 8; note++) {
            uint32_t phase = 0;
            uint32_t inc   = note_phase_inc[note];

            for (uint32_t i = 0; i < NOTE_SAMPLES; i++) {
                buf[i] = sine256[(phase >> 16) & 0xFFU];
                phase += inc;
            }
            for (uint32_t i = NOTE_SAMPLES; i < BUF_SAMPLES; i++) {
                buf[i] = 0;
            }
            mono_to_stereo(buf, BUF_SAMPLES);

            LOG_SIMPLE("nau881x: play_scale: %s", note_names[note]);
            int ret = nau881x_audio_play_start(buf, 2 * BUF_SAMPLES, vol_db);
            if (ret != NAU881X_AUDIO_OK) {
                LOG_SIMPLE("nau881x: play_scale: play_start failed %d", ret);
                overall_ret = -1;
                break;
            }
            uint32_t tmo = (BUF_SAMPLES / 16U) + 1000U;
            ret = nau881x_audio_play_wait(tmo);
            nau881x_audio_play_stop();
            if (ret != NAU881X_AUDIO_OK) {
                LOG_SIMPLE("nau881x: play_scale: timeout on note %d", note);
                overall_ret = -1;
                break;
            }
        }

        hal_mem_free(buf);
        if (overall_ret == 0) LOG_SIMPLE("nau881x: play_scale: done");
        return overall_ret;
    }

    /* ---- dump ---- */
    if (strcmp(sub, "dump") == 0) {
        NAU881x_t *h = get_handle_or_err();
        if (h == NULL) return -1;
        LOG_SIMPLE("nau881x register cache dump:");
        LOG_SIMPLE("  [%02d] PWR1       = 0x%03X", NAU881X_REG_POWER_MANAGEMENT_1,  h->_register[NAU881X_REG_POWER_MANAGEMENT_1]);
        LOG_SIMPLE("  [%02d] PWR2       = 0x%03X", NAU881X_REG_POWER_MANAGEMENT_2,  h->_register[NAU881X_REG_POWER_MANAGEMENT_2]);
        LOG_SIMPLE("  [%02d] PWR3       = 0x%03X", NAU881X_REG_POWER_MANAGEMENT_3,  h->_register[NAU881X_REG_POWER_MANAGEMENT_3]);
        LOG_SIMPLE("  [%02d] AUDIO_IFACE= 0x%03X", NAU881X_REG_AUDIO_INTERFACE,     h->_register[NAU881X_REG_AUDIO_INTERFACE]);
        LOG_SIMPLE("  [%02d] CLK1       = 0x%03X", NAU881X_REG_CLOCK_CTRL_1,        h->_register[NAU881X_REG_CLOCK_CTRL_1]);
        LOG_SIMPLE("  [%02d] CLK2       = 0x%03X", NAU881X_REG_CLOCK_CTRL_2,        h->_register[NAU881X_REG_CLOCK_CTRL_2]);
        LOG_SIMPLE("  [%02d] DAC_CTRL   = 0x%03X", NAU881X_REG_DAC_CTRL,            h->_register[NAU881X_REG_DAC_CTRL]);
        LOG_SIMPLE("  [%02d] DAC_VOL    = 0x%03X", NAU881X_REG_DAC_VOL,             h->_register[NAU881X_REG_DAC_VOL]);
        LOG_SIMPLE("  [%02d] ADC_CTRL   = 0x%03X", NAU881X_REG_ADC_CTRL,            h->_register[NAU881X_REG_ADC_CTRL]);
        LOG_SIMPLE("  [%02d] ADC_VOL    = 0x%03X", NAU881X_REG_ADC_VOL,             h->_register[NAU881X_REG_ADC_VOL]);
        LOG_SIMPLE("  [%02d] INPUT_CTRL = 0x%03X", NAU881X_REG_INPUT_CTRL,          h->_register[NAU881X_REG_INPUT_CTRL]);
        LOG_SIMPLE("  [%02d] PGA_GAIN   = 0x%03X", NAU881X_REG_PGA_GAIN_CTRL,       h->_register[NAU881X_REG_PGA_GAIN_CTRL]);
        LOG_SIMPLE("  [%02d] ADC_BOOST  = 0x%03X", NAU881X_REG_ADC_BOOST_CTRL,      h->_register[NAU881X_REG_ADC_BOOST_CTRL]);
        LOG_SIMPLE("  [%02d] OUT_CTRL   = 0x%03X", NAU881X_REG_OUTPUT_CTRL,         h->_register[NAU881X_REG_OUTPUT_CTRL]);
        LOG_SIMPLE("  [%02d] SPK_MIX    = 0x%03X", NAU881X_REG_SPK_MIXER_CTRL,      h->_register[NAU881X_REG_SPK_MIXER_CTRL]);
        LOG_SIMPLE("  [%02d] SPK_VOL    = 0x%03X", NAU881X_REG_SPK_VOL_CTRL,        h->_register[NAU881X_REG_SPK_VOL_CTRL]);
        LOG_SIMPLE("  [%02d] MONO_MIX   = 0x%03X", NAU881X_REG_MONO_MIXER_CTRL,     h->_register[NAU881X_REG_MONO_MIXER_CTRL]);
        LOG_SIMPLE("  [%02d] ALC1       = 0x%03X", NAU881X_REG_ALC_CTRL_1,          h->_register[NAU881X_REG_ALC_CTRL_1]);
        LOG_SIMPLE("  [%02d] ALC2       = 0x%03X", NAU881X_REG_ALC_CTRL_2,          h->_register[NAU881X_REG_ALC_CTRL_2]);
        LOG_SIMPLE("  [%02d] ALC3       = 0x%03X", NAU881X_REG_ALC_CTRL_3,          h->_register[NAU881X_REG_ALC_CTRL_3]);
        LOG_SIMPLE("  [%02d] NOISE_GATE = 0x%03X", NAU881X_REG_NOISE_GATE,          h->_register[NAU881X_REG_NOISE_GATE]);
        LOG_SIMPLE("  [%02d] SILICON_REV= 0x%03X", NAU881X_REG_SILICON_REV,         h->_register[NAU881X_REG_SILICON_REV]);
        LOG_SIMPLE("  spk_vol_db = %d dB", (int)NAU881x_Get_Speaker_Volume_db(h));
        return 0;
    }

    LOG_SIMPLE("nau881x: unknown subcommand '%s'", sub);
    print_usage();
    return -1;
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static debug_cmd_reg_t s_nau881x_cmd_table[] = {
    {
        "nau881x",
        "NAU881x audio codec (init/deinit/rev/pga/adc/dac/alc/spk/clock/fmt/audio/play/play_mute/play_stop/record/dump)",
        nau881x_cmd
    },
};

void nau881x_cmd_register(void)
{
    debug_cmdline_register(s_nau881x_cmd_table,
                           (int)(sizeof(s_nau881x_cmd_table) / sizeof(s_nau881x_cmd_table[0])));
}
