#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_VERSION 3
#define ESP_ERR_INVALID_ARG 4
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_INVALID_SIZE 6
static inline const char *esp_err_to_name(esp_err_t result) { (void)result; return "error"; }
