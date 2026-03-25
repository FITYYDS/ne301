#include "codec.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "sai.h"
#include "common_utils.h"
#include "generic_file.h"
#include "debug.h"
#include "SensorExt/nau881x/nau881x_dev.h"
#include "SensorExt/i2c_driver/i2c_driver.h"

extern SAI_HandleTypeDef hsai_BlockA1;
extern SAI_HandleTypeDef hsai_BlockB1;
static codec_t g_codec = {0};

const osThreadAttr_t recordTask_attributes = {
    .name = "recordTask",
    .priority = (osPriority_t) osPriorityHigh7,
    .stack_size = 4 * 1024
};

const osThreadAttr_t playTask_attributes = {
    .name = "playTask",
    .priority = (osPriority_t) osPriorityHigh7,
    .stack_size = 4 * 1024
};

const osThreadAttr_t codecTask_attributes = {
    .name = "codecTask",
    .priority = (osPriority_t) osPriorityNormal,
    .stack_size = 4 * 1024
};

#define AUDIO_BUFFER_SIZE   (4096 * 8)
#define AUDIO_HALF_SIZE     (AUDIO_BUFFER_SIZE / 2)
static uint8_t RecordBuff[AUDIO_BUFFER_SIZE] ALIGN_32 UNCACHED;
static volatile int8_t activeBuffer = -1; // -1: no data, 0: Buff1 ready, 1: Buff2 ready
 
#define PLAY_BUFFER_SIZE    (4096 * 8)
#define PLAY_HALF_SIZE      (PLAY_BUFFER_SIZE / 2)
static uint8_t PlayBuff[PLAY_BUFFER_SIZE] ALIGN_32 UNCACHED;
static volatile int8_t playActiveBuffer = -1; // -1: no data, 0: Buff1 ready, 1: Buff2 ready

void fill_WavHeader(uint8_t* header, uint32_t pcm_data_bytes, 
                   uint16_t channels, uint32_t sample_rate, uint16_t bits_per_sample)
{
    uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    uint16_t block_align = channels * bits_per_sample / 8;
    uint32_t riff_size = 36 + pcm_data_bytes;

    memcpy(header, "RIFF", 4);
    *(uint32_t*)(header+4) = riff_size;
    memcpy(header+8, "WAVE", 4);
    memcpy(header+12, "fmt ", 4);
    *(uint32_t*)(header+16) = 16; // fmt chunk size
    *(uint16_t*)(header+20) = 1;  // PCM
    *(uint16_t*)(header+22) = channels;
    *(uint32_t*)(header+24) = sample_rate;
    *(uint32_t*)(header+28) = byte_rate;
    *(uint16_t*)(header+32) = block_align;
    *(uint16_t*)(header+34) = bits_per_sample;
    memcpy(header+36, "data", 4);
    *(uint32_t*)(header+40) = pcm_data_bytes;
}

static int parse_WavHeader(uint8_t *header)
{
    if(header == NULL)
        return -1;
    if(memcmp(header, "RIFF", 4)==0 && memcmp(header+8, "WAVE", 4)==0) {
        // Parse parameters
        // uint16_t channels = *(uint16_t*)(header+22);
        uint32_t sample_rate = *(uint32_t*)(header+24);
        // uint16_t bits_per_sample = *(uint16_t*)(header+34);
        // Configure audio chip and I2S
        // NAU881x_Config(&g_codec.NAU881x, sample_rate, bits_per_sample, channels);
        if(sample_rate != 16000) {
            LOG_SIMPLE("Error: Only 16KHz sample rate is supported! (Current: %lu)\n", sample_rate);
            return -1;
        }
        return 0;
    }
    return -1;

}

void NAU881x_StartRecord(NAU881x_t* nau881x)
{
    // 1. Enable MICBIAS
    NAU881x_Set_MicBias_Enable(nau881x, 1);

    // 2. Select MIC input
    NAU881x_Set_PGA_Input(nau881x, NAU881X_INPUT_MICP | NAU881X_INPUT_MICN);

    // 3. Enable PGA
    NAU881x_Set_PGA_Enable(nau881x, 1);

    // 4. Set PGA gain (adjustable)
    NAU881x_Set_PGA_Gain(nau881x, 48); // 12dB

    // 5. Enable BOOST
    NAU881x_Set_Boost_Enable(nau881x, 1);
    NAU881x_Set_Boost_Volume(nau881x, NAU881X_INPUT_MICP, 0x03); // 3 is medium gain

    // 6. Enable ADC
    NAU881x_Set_ADC_Enable(nau881x, 1);

    // 7. Enable high-pass filter (remove DC/low-frequency noise)
    NAU881x_Set_ADC_HighPassFilter(nau881x, 1, 0, 0x01);

    // // 8. Set I2S interface format
    // NAU881x_Set_AudioInterfaceFormat(nau881x, NAU881X_AUDIO_IFACE_FMT_I2S, NAU881X_AUDIO_IFACE_WL_16BITS);

    // // 9. Configure master clock (codec as slave mode, STM32 as I2S master)
    // NAU881x_Set_Clock(nau881x, 0, NAU881X_BCLKDIV_8, NAU881X_MCLKDIV_1, NAU881X_CLKSEL_MCLK);
}

void NAU881x_SetMicVolume_Percent(NAU881x_t* nau881x, uint8_t percent)
{
    if(percent == 0)
    {
        NAU881x_Set_PGA_Mute(nau881x, 1); // Mute
        return;
    }
    NAU881x_Set_PGA_Mute(nau881x, 0);    // Unmute

    // percent: 1~100 mapped to register 1~63
    uint8_t regval = (uint8_t)(1 + (percent - 1) * 62 / 99); // 1~63
    NAU881x_Set_PGA_Gain(nau881x, regval);
}

void NAU881x_SetSpeakerVolume_Percent(NAU881x_t* nau881x, uint8_t percent)
{
    if(percent == 0)
    {
        NAU881x_Set_Speaker_Mute(nau881x, 1); // Mute
        return;
    }
    NAU881x_Set_Speaker_Mute(nau881x, 0);    // Unmute

    // percent: 1~100 mapped to register 1~63
    uint8_t regval = (uint8_t)(1 + (percent - 1) * 62 / 99); // 1~63
    // 0dB is 57, -57dB is 0, +6dB is 63
    int8_t vol_db = -57 + (regval * 1); // Step 1dB
    // Avoid driving speaker amplifier above 0 dB to reduce loud-level distortion / clicks
    if (vol_db > 0) {
        vol_db = 0;
    }
    NAU881x_Set_Speaker_Volume_db(nau881x, vol_db);
}

void NAU881x_SetDACVolume_Percent(NAU881x_t* nau881x, uint8_t percent)
{
    if(percent == 0)
    {
        NAU881x_Set_DAC_SoftMute(nau881x, 1); // Mute
        return;
    }
    NAU881x_Set_DAC_SoftMute(nau881x, 0);    // Unmute

    // percent: 1~100 mapped to 1~255
    uint8_t regval = (uint8_t)(1 + (percent - 1) * 254 / 99); // 1~255
    NAU881x_Set_DAC_Gain(nau881x, regval);
}


void NAU881x_StopRecord(NAU881x_t* nau881x)
{
    // Disable ADC
    NAU881x_Set_ADC_Enable(nau881x, 0);
    // Disable PGA
    NAU881x_Set_PGA_Enable(nau881x, 0);
}

void NAU881x_StartPlayback(NAU881x_t* nau881x)
{
    NAU881x_Set_DAC_Gain(nau881x, 0xFF);
    NAU881x_Set_DAC_SoftMute(nau881x, 0);
    NAU881x_Set_Speaker_Source(nau881x, NAU881X_OUTPUT_FROM_DAC);
    NAU881x_Set_Speaker_Volume_db(nau881x, 0);
    NAU881x_Set_Speaker_Mute(nau881x, 0);
    /* VDDSPK=5V: enable SPK boost for full output swing (OUTPUT_CTRL bit 2) */
    NAU881x_Set_Speaker_Boost(nau881x, 1);
    NAU881x_Set_Mono_Boost(nau881x, 1);  /* VDDSPK 5V: MOUTBST=1 per Power Up table */
    NAU881x_Set_Output_Enable(nau881x, NAU881X_OUTPUT_SPK);
}

void NAU881x_StopPlayback(NAU881x_t* nau881x)
{
    NAU881x_Set_Speaker_Mute(nau881x, 1);
    NAU881x_Set_DAC_SoftMute(nau881x, 1);
    NAU881x_Set_Output_Enable(nau881x, NAU881X_OUTPUT_NONE);
}

nau881x_status_t NAU881x_Enable_Mic_Bypass_To_SPK(NAU881x_t* nau881x)
{
    nau881x_status_t status = NAU881X_STATUS_OK;

    NAU881x_Set_PGA_Input(nau881x, NAU881X_INPUT_MICP | NAU881X_INPUT_MICN);
    // 2. Enable PGA
    NAU881x_Set_PGA_Enable(nau881x, 1);
    // 3. PGA gain maximum
    NAU881x_Set_PGA_Gain(nau881x, 0x3F); // 35.25dB
    // 4. Enable Boost, gain maximum
    NAU881x_Set_Boost_Enable(nau881x, 1);
    NAU881x_Set_Boost_Volume(nau881x, NAU881X_INPUT_MICP, 0x07);
    // 5. Enable MICBIAS
    NAU881x_Set_MicBias_Enable(nau881x, 1);
    // 6. SPK Mixer select bypass
    NAU881x_Set_Speaker_Source(nau881x, NAU881X_OUTPUT_FROM_BYPASS);
    // 7. Enable SPK output
    NAU881x_Set_Output_Enable(nau881x, NAU881X_OUTPUT_SPK);
    // 8. SPK volume maximum
    NAU881x_Set_Speaker_Volume(nau881x, 0x3F);
    // 9. Disable SPK mute
    NAU881x_Set_Speaker_Mute(nau881x, 0);

    return status;
}

nau881x_status_t NAU881x_Disable_Mic_Bypass_To_SPK(NAU881x_t* nau881x)
{
    nau881x_status_t status = NAU881X_STATUS_OK;

    NAU881x_Set_PGA_Input(nau881x, NAU881X_INPUT_MICP | NAU881X_INPUT_MICN);
    NAU881x_Set_PGA_Enable(nau881x, 1);
    NAU881x_Set_PGA_Gain(nau881x, 0x3F); // 35.25dB
    NAU881x_Set_Boost_Enable(nau881x, 1);
    NAU881x_Set_Boost_Volume(nau881x, NAU881X_INPUT_MICP, 0x07);
    NAU881x_Set_MicBias_Enable(nau881x, 1);
    // Key: bypass to SPK
    NAU881x_Set_Speaker_Source(nau881x, (1 << 5)); // Or use NAU881X_OUTPUT_FROM_BYPASS
    NAU881x_Set_Output_Enable(nau881x, NAU881X_OUTPUT_SPK);
    NAU881x_Set_Speaker_Volume(nau881x, 0x3F);
    NAU881x_Set_Speaker_Mute(nau881x, 0);

    return status;
}

/**
  * @brief  SAI Rx transfer complete callback.
  * @param  hsai SAI handle.
  * @retval None.
  */
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_A) {
        activeBuffer = 1;
    }
}

/**
  * @brief  SAI Rx half transfer complete callback.
  * @param  hsai SAI handle.
  * @retval None.
  */
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_A) {
        activeBuffer = 0;
    }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_B) {
        playActiveBuffer = 1;
    }
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai->Instance == SAI1_Block_B) {
        playActiveBuffer = 0;
    }
}

/**
  * @brief  SAI error callback.
  * @param  hsai SAI handle.
  * @retval None.
  */
void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai) 
{
  uint32_t err = hsai->ErrorCode;
  if(err & HAL_SAI_ERROR_OVR) {
    HAL_SAI_DMAStop(hsai);
    // if(hsai == &hsai_BlockA1) HAL_SAI_Receive_DMA(hsai, ...);
    // else HAL_SAI_Transmit_DMA(hsai, ...);
    if (g_codec.play_state == CODEC_RUNNING) {
        g_codec.play_stop_flag = true;
    }

    if (g_codec.record_state == CODEC_RUNNING) {
        g_codec.record_stop_flag = true;
    }
  }
}

/* I2C access is provided by nau881x_dev (see nau881x_dev.h). */

int record_cmd(int argc, char* argv[])
{
    if (g_codec.record_state == CODEC_RUNNING) {
        LOG_SIMPLE("Already recording!\r\n");
        return -1;
    }
    const char *filename = (argc >= 2) ? argv[1] : "record.wav";
    int rec_time = (argc >= 3) ? atoi(argv[2]) : DEFAULT_RECORD_TIME;
    if (rec_time <= 0) rec_time = DEFAULT_RECORD_TIME;

    strncpy(g_codec.record_filename, filename, sizeof(g_codec.record_filename)-1);
    g_codec.record_filename[sizeof(g_codec.record_filename)-1] = '\0';
    g_codec.record_time = rec_time;   
    g_codec.record_state = CODEC_RUNNING;
    g_codec.record_stop_flag = false; 
    osSemaphoreRelease(g_codec.record_sem);
    LOG_SIMPLE("Start recording: %s, %d seconds\r\n", g_codec.record_filename, g_codec.record_time);
    return 0;
}

int stop_record_cmd(int argc, char* argv[])
{
    if (g_codec.record_state == CODEC_RUNNING) {
        g_codec.record_stop_flag = true;
        LOG_SIMPLE("Stop recording.\r\n");
    } else {
        LOG_SIMPLE("Not recording.\r\n");
    }
    return 0;
}
int play_cmd(int argc, char* argv[])
{
    if (g_codec.play_state == CODEC_RUNNING) {
        LOG_SIMPLE("Already playing!\r\n");
        return -1;
    }
    const char *filename = (argc >= 2) ? argv[1] : "record.wav";
    strncpy(g_codec.play_filename, filename, sizeof(g_codec.play_filename)-1);
    g_codec.play_filename[sizeof(g_codec.play_filename)-1] = '\0';
    g_codec.play_state = CODEC_RUNNING;
    g_codec.play_stop_flag = false; 
    osSemaphoreRelease(g_codec.play_sem);
    return 0;
}

int stop_play_cmd(int argc, char* argv[])
{
    if (g_codec.play_state == CODEC_RUNNING) {
        g_codec.play_stop_flag = true;
        LOG_SIMPLE("Stop playback.\r\n");
    } else {
        LOG_SIMPLE("Not playing.\r\n");
    }
    return 0;
}

static int micvol_cmd(int argc, char* argv[])
{
    if(argc < 2) {
        LOG_SIMPLE("Usage: micvol [0-100]\r\n");
        return -1;
    }
    uint8_t percent = atoi(argv[1]);
    NAU881x_SetMicVolume_Percent(g_codec.nau881x, percent);
    LOG_SIMPLE("Mic volume set to %d%%\r\n", percent);
    return 0;
}

static int spkvol_cmd(int argc, char* argv[])
{
    if(argc < 2) {
        LOG_SIMPLE("Usage: spkvol [0-100]\r\n");
        return -1;
    }
    uint8_t percent = atoi(argv[1]);
    NAU881x_SetSpeakerVolume_Percent(g_codec.nau881x, percent);
    LOG_SIMPLE("Speaker volume set to %d%%\r\n", percent);
    return 0;
}

static int dacvol_cmd(int argc, char* argv[])
{
    if(argc < 2) {
        LOG_SIMPLE("Usage: dacvol [0-100]\r\n");
        return -1;
    }
    uint8_t percent = atoi(argv[1]);
    NAU881x_SetDACVolume_Percent(g_codec.nau881x, percent);
    LOG_SIMPLE("DAC volume set to %d%%\r\n", percent);
    return 0;
}

static int bypass_cmd(int argc, char* argv[])
{
    if(argc < 2) {
        LOG_SIMPLE("Usage: bypass [on|off]\r\n");
        return -1;
    }
    if(strcmp(argv[1], "on") == 0) {
        if (NAU881x_Enable_Mic_Bypass_To_SPK(g_codec.nau881x) == NAU881X_STATUS_OK) {
            LOG_SIMPLE("MIC bypass to SPK enabled.\r\n");
        } else {
            LOG_SIMPLE("Failed to enable bypass!\r\n");
            return -1;
        }
    } else if(strcmp(argv[1], "off") == 0) {
        if (NAU881x_Disable_Mic_Bypass_To_SPK(g_codec.nau881x) == NAU881X_STATUS_OK) {
            LOG_SIMPLE("MIC bypass to SPK disabled.\r\n");
        } else {
            LOG_SIMPLE("Failed to disable bypass!\r\n");
            return -1;
        }
    } else {
        LOG_SIMPLE("Usage: bypass [on|off]\r\n");
        return -1;
    }
    return 0;
}

static int audiotest_cmd(int argc, char* argv[])
{
    int ret = 0;
    uint8_t *read_buf1 = NULL;
    uint8_t *read_buf2 = NULL;
    if(argc < 2) {
        LOG_SIMPLE("Usage: audiotest [file]\r\n");
        return -1;
    }

    read_buf1 = (uint8_t *)hal_mem_alloc_aligned(PLAY_BUFFER_SIZE, 32, MEM_LARGE);
    read_buf2 = (uint8_t *)hal_mem_alloc_aligned(AUDIO_BUFFER_SIZE, 32, MEM_LARGE);
    if (!read_buf1 || !read_buf2) {
        LOG_SIMPLE("Cannot malloc read_buf1 or read_buf2\r\n");
        if (read_buf1) hal_mem_free(read_buf1);
        if (read_buf2) hal_mem_free(read_buf2);
        return -1;
    }

    const char *filename = argv[1];
    FILE *file = file_fopen(filename, "rb");
    if (!file) {
        LOG_SIMPLE("Cannot open file: %s\r\n", filename);
        if (read_buf1) hal_mem_free(read_buf1);
        if (read_buf2) hal_mem_free(read_buf2);
        return -1;
    }
    
    file_fseek(file, 0, SEEK_SET);
    ret = file_fread(file, read_buf1, PLAY_HALF_SIZE);
    if (ret != PLAY_HALF_SIZE) {
        LOG_SIMPLE("Read failed: %d\r\n", ret);
        if (read_buf1) hal_mem_free(read_buf1);
        if (read_buf2) hal_mem_free(read_buf2);
        return -1;
    }
    ret = file_fread(file, read_buf1 + PLAY_HALF_SIZE, PLAY_HALF_SIZE);
    if (ret != PLAY_HALF_SIZE) {
        LOG_SIMPLE("Read failed: %d\r\n", ret);
        if (read_buf1) hal_mem_free(read_buf1);
        if (read_buf2) hal_mem_free(read_buf2);
        return -1;
    }
    file_fseek(file, 44, SEEK_SET);
    ret = file_fread(file, read_buf2, AUDIO_HALF_SIZE);
    if (ret != AUDIO_HALF_SIZE) {
        LOG_SIMPLE("Read failed: %d\r\n", ret);
        if (read_buf1) hal_mem_free(read_buf1);
        if (read_buf2) hal_mem_free(read_buf2);
        return -1;
    }
    ret = file_fread(file, read_buf2 + AUDIO_HALF_SIZE, AUDIO_HALF_SIZE);
    if (ret != AUDIO_HALF_SIZE) {
        LOG_SIMPLE("Read failed: %d\r\n", ret);
        if (read_buf1) hal_mem_free(read_buf1);
        if (read_buf2) hal_mem_free(read_buf2);
        return -1;
    }
    file_fclose(file);

    for (int i = 0; i < PLAY_BUFFER_SIZE - 44; i++) {
        if (read_buf1[i + 44] != read_buf2[i]) {
            LOG_SIMPLE("Audio test failed: %s, %d\r\n", filename, i);
            LOG_SIMPLE("read_buf1[%d] = %02X, read_buf2[%d] = %02X\r\n", i + 44, read_buf1[i + 44], i, read_buf2[i]);
            if (read_buf1) hal_mem_free(read_buf1);
            if (read_buf2) hal_mem_free(read_buf2);
            return -1;
        }
    }
    LOG_SIMPLE("Audio test done: %s\r\n", filename);
    if (read_buf1) hal_mem_free(read_buf1);
    if (read_buf2) hal_mem_free(read_buf2);
    return 0;
}

debug_cmd_reg_t audio_cmd_table[] = {
    {"record",    "Start recording",      record_cmd},
    {"stoprec",   "Stop recording",       stop_record_cmd},
    {"play",      "Start playback",       play_cmd},
    {"stopplay",  "Stop playback",        stop_play_cmd},
    {"micvol",    "Set mic volume [0-100]", micvol_cmd},
    {"spkvol",    "Set speaker volume [0-100]", spkvol_cmd},
    {"dacvol",    "Set DAC volume [0-100]", dacvol_cmd},
    {"bypass",    "Enable/disable MIC->SPK bypass", bypass_cmd}, 
    {"audiotest", "Hex dump audio file region", audiotest_cmd},
};

static void codec_cmd_register(void)
{
    debug_cmdline_register(audio_cmd_table, sizeof(audio_cmd_table) / sizeof(audio_cmd_table[0]));
}

static void recordProcess(void *argument)
{
    codec_t *codec = (codec_t *)argument;
    uint8_t wav_header[44] = {0};
    int ret;
    while (!codec->is_init){
        osDelay(1000);
    }

    while (codec->is_init) {
        if (osSemaphoreAcquire(codec->record_sem, osWaitForever) == osOK) {
            if (codec->record_state != CODEC_RUNNING) continue;

            codec->record_fd = file_fopen(codec->record_filename, "wb+");
            if (!codec->record_fd) {
                LOG_DRV_DEBUG("Cannot open file: %s\r\n", codec->record_filename);
                codec->record_state = CODEC_IDLE;
                continue;
            }
            file_fwrite(codec->record_fd, wav_header, 44);
            codec->record_total_bytes = 0;
            codec->record_stop_flag = false;

            memset((void*)RecordBuff, 0, AUDIO_BUFFER_SIZE);
            if(codec->play_state != CODEC_RUNNING){
                uint8_t TxData[2] = {0x00U, 0x00U};
                /* If no playback is on going, transmit some bytes to generate SAI clock and synchro signals */
                if (HAL_SAI_Transmit(&hsai_BlockB1, TxData, 2, 1000) != HAL_OK){
                    codec->record_stop_flag = true;
                }
            }
            NAU881x_StartRecord(g_codec.nau881x);
            HAL_SAI_Receive_DMA(&hsai_BlockA1, RecordBuff, AUDIO_HALF_SIZE);
            LOG_DRV_DEBUG("Recording to %s, use stoprec to stop...\r\n", codec->record_filename);

            uint32_t start_tick = osKernelGetTickCount();
            uint32_t max_ticks = codec->record_time * 1000; // ms

            while (!codec->record_stop_flag) {
                if (activeBuffer >= 0) {
                    uint8_t* src = (activeBuffer == 0) ? RecordBuff : (RecordBuff + AUDIO_HALF_SIZE);
                    size_t bytesToWrite = AUDIO_HALF_SIZE;
                    
                    ret = file_fwrite(codec->record_fd, src, bytesToWrite);
                    if (ret != bytesToWrite) {
                        LOG_DRV_DEBUG("Write failed: %d/%d\n", ret, bytesToWrite);
                    }
                    // memset(src, 0, bytesToWrite);
                    codec->record_total_bytes += bytesToWrite;
                    activeBuffer = -1; // Reset flag
                }
                
                osDelay(1);

                // Check if specified recording duration is reached
                uint32_t elapsed = osKernelGetTickCount() - start_tick;
                if (elapsed >= max_ticks) {
                    codec->record_stop_flag = true;
                    LOG_DRV_DEBUG("Record time up (%d seconds), auto stop.\r\n", codec->record_time);
                }
            }
            HAL_SAI_DMAStop(&hsai_BlockA1);
            NAU881x_StopRecord(g_codec.nau881x);

            fill_WavHeader(wav_header, codec->record_total_bytes, 2, 16000, 16);
            file_fseek(codec->record_fd, 0, SEEK_SET);
            file_fwrite(codec->record_fd, wav_header, 44);
            file_fflush(codec->record_fd);
            file_fclose(codec->record_fd);

            LOG_DRV_DEBUG("Recording saved: %s, %d bytes\r\n", codec->record_filename, (int)codec->record_total_bytes + 44);
            codec->record_state = CODEC_IDLE;
        }
    }
    osThreadExit();
}

static void playProcess(void *argument)
{
    codec_t *codec = (codec_t *)argument;
    uint8_t wav_header[44];

    while (!codec->is_init){
        osDelay(1000);
    }

    while (codec->is_init) {
        if (osSemaphoreAcquire(codec->play_sem, osWaitForever) == osOK) {
            if (codec->play_state != CODEC_RUNNING) continue;

            codec->play_fd = file_fopen(codec->play_filename, "rb");
            if (!codec->play_fd) {
                LOG_DRV_DEBUG("Cannot open file: %s\r\n", codec->play_filename);
                codec->play_state = CODEC_IDLE;
                continue;
            }
            if (file_fread(codec->play_fd, wav_header, 44) != 44) {
                LOG_DRV_DEBUG("Read wav header failed\r\n");
                file_fclose(codec->play_fd);
                codec->play_state = CODEC_IDLE;
                continue;
            }
            if(parse_WavHeader(wav_header) !=0){
                file_fclose(codec->play_fd);
                codec->play_state = CODEC_IDLE;
                continue;
            }
            codec->play_stop_flag = false;

            NAU881x_StartPlayback(g_codec.nau881x);

            // Pre-fill two buffers
            file_fread(codec->play_fd, PlayBuff, PLAY_BUFFER_SIZE);

            // Start DMA, transfer entire double buffer
            HAL_SAI_Transmit_DMA(&hsai_BlockB1, PlayBuff, PLAY_HALF_SIZE);

            int play_done = 0;
            while (!codec->play_stop_flag && !play_done) {
                if (playActiveBuffer == 0) {
                    // Fill PlayBuff1
                    int read_bytes = file_fread(codec->play_fd, PlayBuff, PLAY_HALF_SIZE);
                    if (read_bytes < 0) {
                        LOG_DRV_DEBUG("Read failed: %d\r\n", read_bytes);
                        play_done = 1;
                    } else if (read_bytes < PLAY_HALF_SIZE) {
                        // End of file, next DMA will finish playback
                        memset((uint8_t*)PlayBuff + read_bytes, 0, PLAY_HALF_SIZE - read_bytes);
                        play_done = 1;
                    }
                    playActiveBuffer = -1;
                } else if (playActiveBuffer == 1) {
                    // Fill PlayBuff2
                    int read_bytes = file_fread(codec->play_fd, PlayBuff + PLAY_HALF_SIZE, PLAY_HALF_SIZE);
                    if (read_bytes < 0) {
                        LOG_DRV_DEBUG("Read failed: %d\r\n", read_bytes);
                        play_done = 1;
                    } else if (read_bytes < PLAY_HALF_SIZE) {
                        memset((uint8_t*)PlayBuff + PLAY_HALF_SIZE + read_bytes, 0, PLAY_HALF_SIZE - read_bytes);
                        play_done = 1;
                    }
                    playActiveBuffer = -1;
                }
                osDelay(1); 
            }
            HAL_SAI_DMAStop(&hsai_BlockB1);
            NAU881x_StopPlayback(g_codec.nau881x);

            file_fclose(codec->play_fd);
            LOG_DRV_DEBUG("Play finished: %s\r\n", codec->play_filename);
            codec->play_state = CODEC_IDLE;
        }
    }
    osThreadExit();
}

static void codecProcess(void *argument)
{
    codec_t *codec = (codec_t *)argument;
    uint8_t silicon_rev = 0;
    LOG_DRV_DEBUG("codecProcess start\r\n");
    // pwr_manager_acquire(codec->pwr_handle);
    MX_SAI1_Init();
    LOG_DRV_DEBUG("MX_SAI1_Init end\r\n");

    /* Initialize I2C driver and NAU881x codec via nau881x_dev (I2C backend). */
    int ret = i2c_driver_init(I2C_PORT_1);
    if (ret != 0) {
        LOG_DRV_DEBUG("codec: i2c_driver_init failed %d\r\n", ret);
        goto exit_thread;
    }

    ret = nau881x_dev_init();
    if (ret != 0) {
        LOG_DRV_DEBUG("codec: nau881x_dev_init failed %d\r\n", ret);
        i2c_driver_deinit(I2C_PORT_1);
        goto exit_thread;
    }

    codec->nau881x = nau881x_dev_get_handle();
    if (codec->nau881x == NULL) {
        LOG_DRV_DEBUG("nau881x_dev_get_handle returned NULL\r\n");
        goto exit_thread;
    }

    osDelay(100);
    NAU881x_Get_SiliconRevision(codec->nau881x, &silicon_rev);
    LOG_DRV_DEBUG("NAU881x Silicon Revision: 0x%02X\r\n", silicon_rev);

    NAU881x_Set_AudioInterfaceFormat(codec->nau881x, NAU881X_AUDIO_IFACE_FMT_I2S, NAU881X_AUDIO_IFACE_WL_16BITS);
    NAU881x_Set_Clock(codec->nau881x, 0, NAU881X_BCLKDIV_8, NAU881X_MCLKDIV_1, NAU881X_CLKSEL_MCLK);  /* slave */
    NAU881x_Set_SampleRate(codec->nau881x, 3);  /* 16 kHz */
    NAU881x_Set_DAC_SoftMute(codec->nau881x, 1);
    NAU881x_Set_DAC_Enable(codec->nau881x, 1);

    osDelay(50);

    codec->is_init = true;
    while (codec->is_init) {
        if (osSemaphoreAcquire(codec->sem_id, osWaitForever) == osOK) {
        
        }
    }

exit_thread:
    osThreadExit();
}


static int codec_ioctl(void *priv, unsigned int cmd, unsigned char* ubuf, unsigned long arg)
{
    codec_t *codec = (codec_t *)priv;
    if(!codec->is_init)
        return -1;
    osMutexAcquire(codec->mtx_id, osWaitForever);

    osMutexRelease(codec->mtx_id);
    return 0;
}

static int codec_init(void *priv)
{
    LOG_DRV_DEBUG("codec_init \r\n");
    codec_t *codec = (codec_t *)priv;

    if (g_codec.is_init) return -1; // Prevent duplicate initialization

    memset((void*)RecordBuff, 0, sizeof(RecordBuff));
    memset((void*)PlayBuff, 0, sizeof(PlayBuff));
    activeBuffer = -1;
    playActiveBuffer = -1;

    codec->mtx_id = osMutexNew(NULL);
    codec->record_state = CODEC_IDLE;
    codec->play_state = CODEC_IDLE;
    codec->record_sem = osSemaphoreNew(1, 0, NULL);
    codec->play_sem = osSemaphoreNew(1, 0, NULL);
    codec->sem_id = osSemaphoreNew(1, 0, NULL);

    // codec->pwr_handle = pwr_manager_get_handle(PWR_CODEC_NAME);

    codec->record_processId = osThreadNew(recordProcess, codec, &recordTask_attributes);
    codec->play_processId = osThreadNew(playProcess, codec, &playTask_attributes);
    codec->codec_processId = osThreadNew(codecProcess, codec, &codecTask_attributes);
    return 0;
}

static int codec_deinit(void *priv)
{
    codec_t *codec = (codec_t *)priv;

    codec->is_init = false;

    // Notify threads to exit
    osSemaphoreRelease(codec->record_sem);
    osSemaphoreRelease(codec->play_sem);
    osSemaphoreRelease(codec->sem_id);
    osDelay(100); // Wait for threads to exit naturally

    // Terminate recording thread
    if (codec->record_processId != NULL) {
        osThreadTerminate(codec->record_processId);
        codec->record_processId = NULL;
    }

    // Terminate playback thread
    if (codec->play_processId != NULL) {
        osThreadTerminate(codec->play_processId);
        codec->play_processId = NULL;
    }

    // Delete semaphores
    if (codec->record_sem != NULL) {
        osSemaphoreDelete(codec->record_sem);
        codec->record_sem = NULL;
    }
    if (codec->play_sem != NULL) {
        osSemaphoreDelete(codec->play_sem);
        codec->play_sem = NULL;
    }

    if (codec->sem_id != NULL) {
        osSemaphoreDelete(codec->sem_id);
        codec->sem_id = NULL;
    }

    // Delete mutex
    if (codec->mtx_id != NULL) {
        osMutexDelete(codec->mtx_id);
        codec->mtx_id = NULL;
    }

    // Release power management
    if (codec->pwr_handle != 0) {
        // pwr_manager_release(codec->pwr_handle);
        codec->pwr_handle = 0;
    }

    return 0;
}

void codec_register(void)
{
    static dev_ops_t codec_ops = {
        .init = codec_init, 
        .deinit = codec_deinit, 
        .ioctl = codec_ioctl
    };
    if(g_codec.is_init == true){
        return;
    }
    device_t *dev = hal_mem_alloc_fast(sizeof(device_t));
    g_codec.dev = dev;
    strcpy(dev->name, CODEC_DEVICE_NAME);
    dev->type = DEV_TYPE_AUDIO;
    dev->ops = &codec_ops;
    dev->priv_data = &g_codec;

    device_register(g_codec.dev);
    
    driver_cmd_register_callback(CODEC_DEVICE_NAME, codec_cmd_register);
}

void codec_unregister(void)
{
    if (g_codec.dev) {
        device_unregister(g_codec.dev);
        hal_mem_free(g_codec.dev);
        g_codec.dev = NULL;
    }
}
