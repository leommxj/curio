#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "curio_config_model.h"
#include "curio_alerts.h"

void *curio_alloc(size_t size);
esp_err_t curio_settings_init(void);
esp_err_t curio_settings_reset(void);
esp_err_t curio_settings_adopt_wifi(const curio_network_config_t *);
char *curio_settings_yaml(uint32_t *revision);
esp_err_t curio_settings_import(const char *yaml, size_t size, uint32_t revision, char *error, size_t error_size);
esp_err_t curio_settings_set_token(bool admin, const char *token);
esp_err_t curio_settings_set_brightness(uint8_t brightness);
esp_err_t curio_settings_set_vibration_strength(uint8_t strength);
esp_err_t curio_settings_set_flash_enabled(bool enabled);
esp_err_t curio_settings_set_volume(uint8_t volume);
esp_err_t curio_settings_set_auto_rotate(bool enabled);
esp_err_t curio_settings_set_page_auto_page(const char *page_id, bool enabled);
