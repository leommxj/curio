#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef unsigned nvs_handle_t;
#define NVS_READWRITE 1
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *name, unsigned mode, nvs_handle_t *handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *size);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_commit(nvs_handle_t handle);

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size);
