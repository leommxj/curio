#include "curio_clock.h"
#include <string.h>

void curio_clock_format(time_t utc, int offset_minutes, bool seconds, curio_clock_text_t *out)
{
    strcpy(out->time, seconds ? "--:--:--" : "--:--");
    out->date[0] = 0;
    if (utc < 1704067200) return;
    time_t local = utc + offset_minutes * 60;
    struct tm value;
    if (!gmtime_r(&local, &value)) return;
    strftime(out->time, sizeof(out->time), seconds ? "%H:%M:%S" : "%H:%M", &value);
    strftime(out->date, sizeof(out->date), "%Y-%m-%d", &value);
}

#ifndef CURIO_CLOCK_FORMAT_ONLY
time_t curio_clock_now(void) { return time(NULL); }
#endif

#ifdef ESP_PLATFORM
#include "esp_netif_sntp.h"
#include "esp_log.h"

void curio_clock_update(bool enabled, bool connected, bool reconnect, const char *server)
{
    static bool running;
    static char active_server[128];
    if (running && (!enabled || !connected || reconnect || strcmp(server, active_server))) {
        esp_netif_sntp_deinit();
        running = false;
    }
    if (!enabled || !connected || running) return;
    strcpy(active_server, server);
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(active_server);
    config.wait_for_sync = false;
    esp_err_t result = esp_netif_sntp_init(&config);
    running = result == ESP_OK;
    if (!running) ESP_LOGW("curio_clock", "SNTP start failed: %s", esp_err_to_name(result));
}
#endif
