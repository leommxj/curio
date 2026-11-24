#pragma once
#include <assert.h>
#include <pthread.h>

curio_app_t curio_app;
static uint8_t stored[2][CURIO_CONFIG_MAX_BYTES + 5];
static size_t stored_size[2];
static unsigned commits;
static unsigned tls_prepares;
static bool fail_write;
static int fail_blob_slot = -1;
static bool reset_pending;
static pthread_mutex_t app_mutex = PTHREAD_MUTEX_INITIALIZER;

esp_err_t curio_tls_prepare(curio_config_t *config)
{
    tls_prepares++;
    if (!strcmp(config->tls.certificate, "bad-cert")) return ESP_ERR_INVALID_ARG;
    if (!*config->tls.certificate) {
        snprintf(config->tls.certificate, sizeof(config->tls.certificate), "generated-cert:%s", config->network.hostname);
        strcpy(config->tls.private_key, "generated-key");
    }
    return ESP_OK;
}

void esp_fill_random(void *buffer, size_t size) { static uint8_t seed; memset(buffer, ++seed, size); }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_open(const char *name, unsigned mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "curio") && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
static unsigned slot(const char *key)
{
    assert(!strcmp(key, "config0") || !strcmp(key, "config1"));
    return key[6] - '0';
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size)
{
    assert(handle == 1);
    unsigned index = slot(key);
    if (!stored_size[index]) return ESP_ERR_NOT_FOUND;
    if (out) { assert(*size >= stored_size[index]); memcpy(out, stored[index], stored_size[index]); }
    *size = stored_size[index];
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    assert(handle == 1 && size <= sizeof(stored[0]));
    assert(pthread_mutex_trylock(curio_app.mutex) == 0);
    pthread_mutex_unlock(curio_app.mutex);
    if (fail_write) return ESP_ERR_INVALID_STATE;
    unsigned index = slot(key);
    if ((int)index == fail_blob_slot) return ESP_ERR_INVALID_STATE;
    memcpy(stored[index], value, size);
    stored_size[index] = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle == 1); commits++; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    assert(handle == 1 && !strcmp(key, "reset"));
    if (!reset_pending) return ESP_ERR_NVS_NOT_FOUND;
    *value = 1;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    assert(handle == 1 && !strcmp(key, "reset") && value == 1);
    if (fail_write) return ESP_ERR_INVALID_STATE;
    reset_pending = true;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 1 && !strcmp(key, "reset"));
    if (fail_write) return ESP_ERR_INVALID_STATE;
    reset_pending = false;
    return ESP_OK;
}
