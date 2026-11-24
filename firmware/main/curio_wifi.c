#include "curio_wifi.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "mosaico_shared.h"
#include "nvs.h"
#include <string.h>

bool curio_wifi_load_shared(curio_network_config_t *network)
{
    curio_wifi_config_t entry = {0};
    if (mosaico_shared_get("wifi_ssid", entry.ssid, sizeof(entry.ssid)) != ESP_OK || !entry.ssid[0]) return false;
    esp_err_t result = mosaico_shared_get("wifi_password", entry.password, sizeof(entry.password));
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) return false;
    size_t length = strlen(entry.password);
    if ((length && length < 8) || (length == 64 && strspn(entry.password, "0123456789abcdefABCDEF") != 64)) return false;
    network->wifi[0] = entry;
    network->wifi_count = 1;
    return true;
}

static void next_network(curio_wifi_t *wifi, size_t count, uint64_t now)
{
    wifi->index = (wifi->index + 1) % count;
    wifi->phase = CURIO_WIFI_WAIT;
    wifi->deadline = now + 2000;
}

void curio_wifi_update(curio_wifi_t *wifi, const curio_network_config_t *network,
                       uint64_t now, bool connected, bool disconnected)
{
    if (!network->wifi_count) return;
    if (connected && wifi->phase != CURIO_WIFI_STOPPING) {
        wifi->phase = CURIO_WIFI_ONLINE;
        return;
    }
    if (disconnected) {
        if (wifi->phase == CURIO_WIFI_CONNECTING) next_network(wifi, network->wifi_count, now);
        else if (wifi->phase == CURIO_WIFI_ONLINE) {
            wifi->phase = CURIO_WIFI_WAIT;
            wifi->deadline = now + 2000;
        } else if (wifi->phase == CURIO_WIFI_STOPPING) wifi->phase = CURIO_WIFI_WAIT;
    }
    if (wifi->phase == CURIO_WIFI_ONLINE || now < wifi->deadline) return;
    if (wifi->phase == CURIO_WIFI_CONNECTING) {
        esp_wifi_disconnect();
        next_network(wifi, network->wifi_count, now);
        wifi->phase = CURIO_WIFI_STOPPING;
        return;
    }
    const curio_wifi_config_t *entry = &network->wifi[wifi->index];
    wifi_config_t station = {0};
    memcpy(station.sta.ssid, entry->ssid, strlen(entry->ssid));
    memcpy(station.sta.password, entry->password, strlen(entry->password));
    station.sta.pmf_cfg.capable = true;
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &station);
    if (result == ESP_OK) result = esp_wifi_connect();
    if (result != ESP_OK) {
        ESP_LOGW("curio_wifi", "Network %u: %s", (unsigned)wifi->index + 1, esp_err_to_name(result));
        next_network(wifi, network->wifi_count, now);
        return;
    }
    ESP_LOGI("curio_wifi", "Connecting to network %u/%u", (unsigned)wifi->index + 1, (unsigned)network->wifi_count);
    wifi->phase = CURIO_WIFI_CONNECTING;
    wifi->deadline = now + 15000;
}
