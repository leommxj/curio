#include "curio_app.h"
#include "esp_iris.h"
#include <stdlib.h>
#include <string.h>

static char *upload;
static size_t uploaded;

esp_err_t curio_restart(void)
{
    esp_err_t result = esp_iris_mark_planned_restart();
    return result == ESP_OK ? esp_iris_schedule_restart(1000) : result;
}

static esp_err_t provision(const esp_iris_rpc_request_t *request, uint8_t *response,
                           size_t capacity, size_t *size, void *context)
{
    (void)response; (void)capacity; (void)context;
    *size = 0;
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    bool allowed = curio_app.setup_active || !curio_app.configured;
    xSemaphoreGive(curio_app.mutex);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    if (request->method_id == 3) {
        if (request->payload_size) return ESP_ERR_INVALID_SIZE;
        free(upload);
        upload = NULL;
        uploaded = 0;
        return curio_restart();
    }
    if (request->method_id == 1) {
        if (request->payload_size <= 4 || request->payload_size > 1024) return ESP_ERR_INVALID_SIZE;
        const uint8_t *p = request->payload;
        uint32_t offset = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        size_t count = request->payload_size - 4;
        if (offset > CURIO_CONFIG_MAX_BYTES || count > CURIO_CONFIG_MAX_BYTES - offset) return ESP_ERR_INVALID_SIZE;
        if (offset && !upload) return ESP_ERR_INVALID_STATE;
        if (!offset) {
            if (!upload) upload = curio_alloc(CURIO_CONFIG_MAX_BYTES);
            uploaded = 0;
        }
        if (!upload) return ESP_ERR_NO_MEM;
        if (offset != uploaded) return ESP_ERR_INVALID_ARG;
        memcpy(upload + uploaded, p + 4, count);
        uploaded += count;
        return ESP_OK;
    }
    if (request->method_id != 2) return ESP_ERR_INVALID_ARG;
    if (request->payload_size) return ESP_ERR_INVALID_SIZE;
    if (!upload || !uploaded) return ESP_ERR_INVALID_STATE;
    char error[128];
    curio_config_t *config = curio_alloc(sizeof(*config));
    esp_err_t result = ESP_ERR_NO_MEM;
    if (config) {
        memset(config, 0, sizeof(*config));
        result = curio_config_parse(upload, uploaded, config, error, sizeof(error)) && config->network.wifi_count
            ? ESP_OK : ESP_ERR_INVALID_ARG;
        curio_config_free(config);
        free(config);
    }
    if (result == ESP_OK) {
        uint32_t revision;
        char *current = curio_settings_yaml(&revision);
        if (!current) result = ESP_ERR_INVALID_STATE;
        else {
            free(current);
            result = curio_settings_import(upload, uploaded, revision, error, sizeof(error));
        }
    }
    free(upload);
    upload = NULL;
    uploaded = 0;
    return result;
}

void curio_provision_start(void)
{
    for (unsigned method = 1; method <= 3; method++)
        ESP_ERROR_CHECK(esp_iris_rpc_register(0x4355, method, provision, NULL));
}
