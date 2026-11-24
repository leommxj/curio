#pragma once

#include "esp_wifi.h"
#include <stddef.h>

esp_err_t mosaico_shared_get(const char *key, char *value, size_t capacity);
