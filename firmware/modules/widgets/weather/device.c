#include "weather.h"
#include "curio_app.h"
#include "curio_widget_service.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BODY_CAPACITY 2048
static esp_http_client_handle_t client;
static char *body;
static size_t body_length;
static bool overflow;
static double pending_latitude, pending_longitude;
static uint64_t started_at;

static esp_err_t receive(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (event->data_len < 0 || (size_t)event->data_len >= BODY_CAPACITY - body_length) {
            overflow = true;
            return ESP_FAIL;
        }
        memcpy(body + body_length, event->data, (size_t)event->data_len);
        body_length += (size_t)event->data_len;
        body[body_length] = '\0';
    }
    return ESP_OK;
}

static void finish(uint64_t now, bool success) {
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_weather_complete(pending_latitude, pending_longitude, now, success ? body : NULL);
    xSemaphoreGive(curio_app.mutex);
    if (client) esp_http_client_cleanup(client);
    client = NULL;
    free(body);
    body = NULL;
}

static void weather_poll(uint64_t now, bool connected) {
    if (client) {
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        bool retained = curio_weather_contains(pending_latitude, pending_longitude);
        xSemaphoreGive(curio_app.mutex);
        if (!connected || !retained || now - started_at >= 30000) {
            if (connected && retained) ESP_LOGW("weather", "Request timed out");
            finish(now, false);
            return;
        }
    } else {
        if (!connected || time(NULL) < 1704067200) return;
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        bool ready = curio_weather_next(now, &pending_latitude, &pending_longitude);
        xSemaphoreGive(curio_app.mutex);
        if (!ready) return;
        body = curio_alloc(BODY_CAPACITY);
        if (!body) { finish(now, false); return; }
        body[0] = '\0';
        body_length = 0;
        overflow = false;
        char url[256];
        snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.8f&longitude=%.8f&current=temperature_2m,relative_humidity_2m,weather_code", pending_latitude, pending_longitude);
        esp_http_client_config_t config = {
            .url = url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = receive,
            .is_async = true,
            .timeout_ms = 5000,
            .disable_auto_redirect = true,
        };
        client = esp_http_client_init(&config);
        if (!client) { finish(now, false); return; }
        started_at = now;
    }
    esp_err_t result = esp_http_client_perform(client);
    /* The TLS transport keeps its initial connect timeout. */
    esp_http_client_set_timeout_ms(client, 1000);
    if (result == ESP_ERR_HTTP_EAGAIN && !overflow) return;
    int status = esp_http_client_get_status_code(client);
    if (result != ESP_OK || overflow || status != 200)
        ESP_LOGW("weather", "Request failed: %s, HTTP %d, %u bytes", esp_err_to_name(result), status, (unsigned)body_length);
    finish(now, result == ESP_OK && !overflow && status == 200);
}

const curio_widget_service_t curio_widget_weather_service = {
    .configure = curio_weather_configure,
    .poll = weather_poll,
};
