#pragma once
#include "esp_codec_dev.h"
#define BSP_AUDIO_DEFAULT_SAMPLE_RATE 16000
#define BSP_AUDIO_DEFAULT_CHANNELS 2
#define BSP_AUDIO_DEFAULT_BITS_PER_SAMPLE 16
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
