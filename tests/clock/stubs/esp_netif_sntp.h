#pragma once
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { const char *server; bool wait_for_sync; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(name) {.server = (name), .wait_for_sync = true}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
void esp_netif_sntp_deinit(void);
