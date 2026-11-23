#include "curio_settings.h"
#include "curio_app.h"
#include "curio_build_config.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nvs_handle_t settings;
static bool ready;
static SemaphoreHandle_t write_mutex;
static char *document;
static uint32_t document_revision;
static const char *TAG = "curio_settings";

void *curio_alloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static esp_err_t alerts_prepare(const curio_alert_config_t *config, curio_alerts_t **runtime)
{
    *runtime = NULL;
    bool enabled = false;
    for (size_t i = 0; i < config->rule_count; i++) enabled |= config->rules[i].enabled;
    if (!enabled) return ESP_OK;
    curio_alerts_t *next = curio_alerts_create((curio_allocator_t){curio_alloc, heap_caps_free});
    if (!next) return ESP_ERR_NO_MEM;
    if (!curio_alerts_configure(next, config)) {
        curio_alerts_destroy(next);
        return ESP_ERR_NO_MEM;
    }
    *runtime = next;
    return ESP_OK;
}

static void snapshot(curio_config_t *config)
{
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    config->device = curio_app.device;
    config->display = curio_app.layout;
    config->alerts = curio_app.alert_config;
    config->access = curio_app.access;
    xSemaphoreGive(curio_app.mutex);
}

static esp_err_t persist(const char *yaml, uint32_t revision)
{
    size_t size = strlen(yaml) + 1;
    uint8_t *blob = curio_alloc(sizeof(revision) + size);
    if (!blob) return ESP_ERR_NO_MEM;
    memcpy(blob, &revision, sizeof(revision));
    memcpy(blob + sizeof(revision), yaml, size);
    char key[16];
    snprintf(key, sizeof(key), "config%u", (unsigned)(revision % 2));
    esp_err_t result = nvs_set_blob(settings, key, blob, sizeof(revision) + size);
    free(blob);
    if (result == ESP_OK) result = nvs_commit(settings);
    if (result != ESP_OK) ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(result));
    return result;
}

static esp_err_t commit(curio_config_t *config)
{
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    if (curio_app.device.auto_rotate && !config->device.auto_rotate)
        config->device.rotation = curio_app.orientation.rotation;
    config->device.revision = curio_app.device.revision;
    config->display.revision = curio_app.layout.revision;
    config->alerts.revision = curio_app.alert_config.revision;
    bool device_changed = config->device.brightness != curio_app.device.brightness ||
        strcmp(config->device.ntp_server, curio_app.device.ntp_server) ||
        config->device.rotation != curio_app.device.rotation || config->device.auto_rotate != curio_app.device.auto_rotate;
    bool display_changed = memcmp(&config->display, &curio_app.layout, sizeof(config->display)) != 0;
    bool rules_changed = config->alerts.rule_count != curio_app.alert_config.rule_count ||
        memcmp(config->alerts.rules, curio_app.alert_config.rules, sizeof(config->alerts.rules)) != 0;
    bool alerts_changed = rules_changed || config->alerts.volume != curio_app.alert_config.volume ||
        config->alerts.vibration_strength != curio_app.alert_config.vibration_strength ||
        config->alerts.flash_enabled != curio_app.alert_config.flash_enabled;
    xSemaphoreGive(curio_app.mutex);
    char *yaml = curio_config_yaml(config);
    if (!yaml) return ESP_ERR_NO_MEM;
    if (document && !strcmp(document, yaml)) { cJSON_free(yaml); return ESP_OK; }
    if (document_revision >= UINT32_MAX - 3) { cJSON_free(yaml); return ESP_ERR_INVALID_VERSION; }
    uint32_t revision = document_revision + 1;
    if (device_changed) config->device.revision = revision;
    if (display_changed) config->display.revision = revision;
    if (alerts_changed) config->alerts.revision = revision;
    curio_alerts_t *next = NULL, *previous = NULL;
    esp_err_t result = rules_changed ? alerts_prepare(&config->alerts, &next) : ESP_OK;
    if (result == ESP_OK) result = persist(yaml, revision);
    if (result == ESP_OK) {
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        curio_app.device = config->device;
        curio_app.layout = config->display;
        curio_app.alert_config = config->alerts;
        curio_app.access = config->access;
        if (rules_changed) { previous = curio_app.alerts; curio_app.alerts = next; next = NULL; }
        else {
            curio_alerts_set_volume(curio_app.alerts, config->alerts.volume);
            curio_alerts_set_vibration_strength(curio_app.alerts, config->alerts.vibration_strength);
        }
        xSemaphoreGive(curio_app.mutex);
        cJSON_free(document);
        document = yaml;
        yaml = NULL;
        document_revision = revision;
    }
    cJSON_free(yaml);
    curio_alerts_destroy(previous);
    curio_alerts_destroy(next);
    return result;
}

static void initial_token(char *out, const char *configured)
{
    if (configured[0]) { snprintf(out, 129, "%s", configured); return; }
    uint8_t random[32];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); i++) snprintf(out + i * 2, 3, "%02x", random[i]);
}

esp_err_t curio_settings_init(void)
{
    ready = false;
    curio_layout_default(&curio_app.layout);
    curio_alert_config_default(&curio_app.alert_config);
    curio_app.device = (curio_device_config_t){.brightness = 75, .ntp_server = "pool.ntp.org"};
    initial_token(curio_app.access.admin_token, CURIO_ADMIN_TOKEN);
    initial_token(curio_app.access.collector_token, CURIO_DEVICE_TOKEN);
    if (!write_mutex) write_mutex = xSemaphoreCreateMutex();
    if (!write_mutex) return ESP_ERR_NO_MEM;
    esp_err_t result = nvs_flash_init();
    if (result == ESP_OK) result = nvs_open("curio", NVS_READWRITE, &settings);
    if (result != ESP_OK) return result;
    curio_config_t *config = curio_alloc(sizeof(*config)), *candidate = curio_alloc(sizeof(*candidate));
    if (!config || !candidate) { free(config); free(candidate); return ESP_ERR_NO_MEM; }
    snapshot(config);
    bool loaded = false;
    document_revision = 0;
    for (unsigned slot = 0; slot < 2; slot++) {
        char key[16], error[128];
        snprintf(key, sizeof(key), "config%u", slot);
        size_t size = 0;
        if (nvs_get_blob(settings, key, NULL, &size) != ESP_OK || size < 6 || size > CURIO_CONFIG_MAX_BYTES + 5) continue;
        uint8_t *blob = curio_alloc(size);
        if (!blob) { result = ESP_ERR_NO_MEM; break; }
        if (nvs_get_blob(settings, key, blob, &size) == ESP_OK && blob[size - 1] == 0) {
            uint32_t revision;
            memcpy(&revision, blob, sizeof(revision));
            if ((!loaded || revision >= document_revision) && revision < UINT32_MAX - 2 &&
                curio_config_parse((char *)blob + 4, size - 5, candidate, error, sizeof(error))) {
                *config = *candidate;
                document_revision = revision;
                loaded = true;
            }
        }
        free(blob);
    }
    free(candidate);
    char *yaml = result == ESP_OK ? curio_config_yaml(config) : NULL;
    if (result == ESP_OK && !yaml) result = ESP_ERR_NO_MEM;
    if (result == ESP_OK && !loaded) {
        document_revision = 1;
        result = persist(yaml, document_revision);
    }
    if (result == ESP_OK) {
        config->device.revision = document_revision;
        config->display.revision = document_revision;
        config->alerts.revision = document_revision;
        curio_alerts_t *next = NULL;
        result = alerts_prepare(&config->alerts, &next);
        if (result == ESP_OK) {
            curio_alerts_destroy(curio_app.alerts);
            curio_app.alerts = next;
            curio_app.device = config->device;
            curio_app.layout = config->display;
            curio_app.alert_config = config->alerts;
            curio_app.access = config->access;
            cJSON_free(document);
            document = yaml;
            yaml = NULL;
            ready = true;
        }
    }
    cJSON_free(yaml);
    free(config);
    return result;
}

char *curio_settings_yaml(uint32_t *revision)
{
    if (!ready) return NULL;
    xSemaphoreTake(write_mutex, portMAX_DELAY);
    size_t size = strlen(document) + 1;
    char *copy = curio_alloc(size);
    if (copy) memcpy(copy, document, size);
    if (revision) *revision = document_revision;
    xSemaphoreGive(write_mutex);
    return copy;
}

esp_err_t curio_settings_import(const char *yaml, size_t size, uint32_t revision, char *error, size_t error_size)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    curio_config_t *config = curio_alloc(sizeof(*config));
    if (!config) return ESP_ERR_NO_MEM;
    if (!curio_config_parse(yaml, size, config, error, error_size)) { free(config); return ESP_ERR_INVALID_ARG; }
    xSemaphoreTake(write_mutex, portMAX_DELAY);
    esp_err_t result = revision == document_revision ? commit(config) : ESP_ERR_INVALID_VERSION;
    xSemaphoreGive(write_mutex);
    free(config);
    return result;
}

typedef enum { CHANGE_BRIGHTNESS, CHANGE_ROTATE, CHANGE_VOLUME, CHANGE_VIBRATION, CHANGE_FLASH, CHANGE_PAGING, CHANGE_ADMIN, CHANGE_COLLECTOR } change_t;
static esp_err_t change(change_t kind, unsigned value, const char *page_id, const char *token)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    curio_config_t *config = curio_alloc(sizeof(*config));
    if (!config) return ESP_ERR_NO_MEM;
    xSemaphoreTake(write_mutex, portMAX_DELAY);
    snapshot(config);
    esp_err_t result = ESP_OK;
    switch (kind) {
    case CHANGE_BRIGHTNESS: config->device.brightness = value; break;
    case CHANGE_ROTATE: config->device.auto_rotate = value; break;
    case CHANGE_VOLUME: config->alerts.volume = value; break;
    case CHANGE_VIBRATION: config->alerts.vibration_strength = value; break;
    case CHANGE_FLASH: config->alerts.flash_enabled = value; break;
    case CHANGE_ADMIN: strcpy(config->access.admin_token, token); break;
    case CHANGE_COLLECTOR: strcpy(config->access.collector_token, token); break;
    case CHANGE_PAGING:
        result = ESP_ERR_NOT_FOUND;
        for (size_t p = 0; p < config->display.page_count; p++) {
            curio_page_config_t *page = &config->display.pages[p];
            if (strcmp(page->id, page_id)) continue;
            for (size_t w = 0; w < page->widget_count; w++) {
                curio_widget_config_t *widget = &page->widgets[w];
                if (widget->type == CURIO_WIDGET_AGENTS) {
                    widget->auto_page = value;
                    result = ESP_OK;
                }
            }
        }
    }
    if (result == ESP_OK) result = commit(config);
    xSemaphoreGive(write_mutex);
    free(config);
    return result;
}

esp_err_t curio_settings_set_brightness(uint8_t value) { return value <= 100 ? change(CHANGE_BRIGHTNESS, value, NULL, NULL) : ESP_ERR_INVALID_ARG; }
esp_err_t curio_settings_set_vibration_strength(uint8_t value) { return value <= 100 ? change(CHANGE_VIBRATION, value, NULL, NULL) : ESP_ERR_INVALID_ARG; }
esp_err_t curio_settings_set_volume(uint8_t value) { return value <= 100 ? change(CHANGE_VOLUME, value, NULL, NULL) : ESP_ERR_INVALID_ARG; }
esp_err_t curio_settings_set_flash_enabled(bool value) { return change(CHANGE_FLASH, value, NULL, NULL); }
esp_err_t curio_settings_set_auto_rotate(bool value) { return change(CHANGE_ROTATE, value, NULL, NULL); }
esp_err_t curio_settings_set_page_auto_page(const char *page_id, bool value)
{
    return page_id ? change(CHANGE_PAGING, value, page_id, NULL) : ESP_ERR_INVALID_ARG;
}
esp_err_t curio_settings_set_token(bool admin, const char *token)
{
    size_t size = token ? strlen(token) : 0;
    if (size < 16 || size > 128) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < size; i++) if ((unsigned char)token[i] < 33 || (unsigned char)token[i] > 126) return ESP_ERR_INVALID_ARG;
    return change(admin ? CHANGE_ADMIN : CHANGE_COLLECTOR, 0, NULL, token);
}
