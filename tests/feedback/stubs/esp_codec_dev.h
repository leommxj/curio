#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_CODEC_DEV_OK 0
typedef void *esp_codec_dev_handle_t;
typedef struct { unsigned sample_rate, bits_per_sample, channel; } esp_codec_dev_sample_info_t;
int esp_codec_dev_open(esp_codec_dev_handle_t handle, const esp_codec_dev_sample_info_t *sample);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, unsigned volume);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t handle, bool mute);
int esp_codec_dev_write(esp_codec_dev_handle_t handle, void *samples, size_t size);
int esp_codec_dev_close(esp_codec_dev_handle_t handle);
