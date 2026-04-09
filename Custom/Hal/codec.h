#ifndef _CODEC_H
#define _CODEC_H

#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "pwr.h"
#include "nau881x.h"

#define SAMPLE_RATE			16000

#define DEFAULT_RECORD_TIME (10)


typedef struct {
    char riff_id[4];      // "RIFF"
    uint32_t riff_size;   // Total file size - 8
    char wave_id[4];      // "WAVE"
    char fmt_id[4];       // "fmt "
    uint32_t fmt_size;    // 16
    uint16_t audio_format;// 1(PCM)
    uint16_t num_channels;// Number of channels
    uint32_t sample_rate; // Sample rate
    uint32_t byte_rate;   // =SampleRate*NumChannels*BitsPerSample/8
    uint16_t block_align; // =NumChannels*BitsPerSample/8
    uint16_t bits_per_sample; // Bit width
    char data_id[4];      // "data"
    uint32_t data_size;   // PCM data length
} WAV_HEADER;

typedef enum {
    CODEC_IDLE = 0,
    CODEC_RUNNING,
    CODEC_STOP
} codec_state_t;

typedef struct {
    // Recording related
    osThreadId_t    record_processId;
    osSemaphoreId_t record_sem;
    volatile bool   record_stop_flag;
    int record_time; // Recording duration, unit: seconds
    codec_state_t   record_state;
    void           *record_fd;
    size_t          record_total_bytes;
    char            record_filename[64];

    // Playback related
    osThreadId_t    play_processId;
    osSemaphoreId_t play_sem;
    volatile bool   play_stop_flag;
    codec_state_t   play_state;
    void           *play_fd;
    char            play_filename[64];

    bool            is_init;
    device_t       *dev;
    osMutexId_t     mtx_id;
    PowerHandle     pwr_handle;
    NAU881x_t      *nau881x;        // NAU881x codec handle from nau881x_dev
    osSemaphoreId_t sem_id;
    osThreadId_t    codec_processId;
} codec_t;


void codec_register(void);
void codec_unregister(void);
#endif