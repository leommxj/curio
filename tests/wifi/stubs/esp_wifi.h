#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define WIFI_IF_STA 0
typedef struct {
    struct {
        uint8_t ssid[32], password[64];
        struct { bool capable; } pmf_cfg;
    } sta;
} wifi_config_t;
esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
