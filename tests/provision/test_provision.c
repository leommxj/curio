#include "../../firmware/main/curio_settings.c"
#include "../../firmware/main/curio_provision.c"
#include <assert.h>
#include <pthread.h>

curio_app_t curio_app;
static pthread_mutex_t app_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t stored[2][CURIO_CONFIG_MAX_BYTES + 5];
static size_t stored_size[2];
static unsigned commits;
static bool fail_write;
static uint8_t reset_pending;
static esp_iris_rpc_handler_t methods[4];
static unsigned planned_restarts, scheduled_restarts;

esp_err_t curio_tls_prepare(curio_config_t *config)
{
    if (!strcmp(config->tls.certificate, "bad-cert")) return ESP_ERR_INVALID_ARG;
    if (!config->tls.certificate[0]) {
        strcpy(config->tls.certificate, "generated-cert");
        strcpy(config->tls.private_key, "generated-key");
    }
    return ESP_OK;
}
void esp_fill_random(void *buffer, size_t size) { memset(buffer, 0x42, size); }
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
    return (unsigned)(key[6] - '0');
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
    if (fail_write) return ESP_ERR_INVALID_STATE;
    unsigned index = slot(key);
    memcpy(stored[index], value, size);
    stored_size[index] = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle == 1); commits++; return ESP_OK; }
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    assert(handle == 1 && !strcmp(key, "reset"));
    *value = reset_pending;
    return reset_pending ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    assert(handle == 1 && !strcmp(key, "reset"));
    if (fail_write) return ESP_ERR_INVALID_STATE;
    reset_pending = value;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 1 && !strcmp(key, "reset"));
    reset_pending = 0;
    return ESP_OK;
}
esp_err_t esp_iris_rpc_register(uint16_t service, uint16_t method, esp_iris_rpc_handler_t handler, void *ctx)
{
    assert(service == 0x4355 && method >= 1 && method <= 3 && !methods[method] && !ctx);
    methods[method] = handler;
    return ESP_OK;
}
esp_err_t esp_iris_mark_planned_restart(void) { planned_restarts++; return ESP_OK; }
esp_err_t esp_iris_schedule_restart(uint32_t delay)
{
    assert(delay == 1000);
    scheduled_restarts++;
    return ESP_OK;
}

static esp_err_t call(unsigned method, const uint8_t *payload, size_t length)
{
    esp_iris_rpc_request_t request = {.service_id = 0x4355, .method_id = method,
                                      .payload = payload, .payload_size = length};
    uint8_t response[1];
    size_t response_size = 999;
    esp_err_t result = methods[method](&request, response, sizeof(response), &response_size, NULL);
    assert(response_size == 0);
    return result;
}
static esp_err_t send_chunk(const char *yaml, size_t total, size_t offset, size_t count)
{
    assert(offset <= total && count <= total - offset && count <= 1020);
    uint8_t payload[1024];
    for (unsigned i = 0; i < 4; i++) payload[i] = (uint8_t)(offset >> (i * 8));
    memcpy(payload + 4, yaml + offset, count);
    return call(1, payload, count + 4);
}
static esp_err_t send_document(const char *yaml)
{
    size_t size = strlen(yaml);
    for (size_t offset = 0; offset < size; offset += 1020) {
        size_t count = size - offset < 1020 ? size - offset : 1020;
        esp_err_t result = send_chunk(yaml, size, offset, count);
        if (result != ESP_OK) return result;
    }
    return call(2, NULL, 0);
}
static void yaml_config(char *out, size_t capacity, const char *ssid, const char *tls)
{
    int n = snprintf(out, capacity,
        "version: 1\nnetwork:\n  wifi:\n    - ssid: %s\n      password: passphrase\n  hostname: curio\n"
        "%s"
        "access:\n  admin_token: abcdefghijklmnop\n  collector_token: abcdefghijklmnop\n"
        "screen:\n  pages:\n    - id: main\n      widgets:\n"
        "        - type: agents\n          at: [0, 0]\n          size: [4, 6]\n",
        ssid, tls);
    assert(n > 0 && (size_t)n < capacity);
}
static void assert_unchanged(const char *before, uint32_t revision, unsigned saved_commits)
{
    uint32_t actual_revision;
    char *current = curio_settings_yaml(&actual_revision);
    assert(current && !strcmp(current, before) && actual_revision == revision && commits == saved_commits);
    free(current);
}
int main(void)
{
    curio_app.mutex = &app_mutex;
    assert(curio_settings_init() == ESP_OK);
    curio_provision_start();
    assert(methods[1] && methods[2] && methods[3]);
    uint32_t revision;
    char *initial = curio_settings_yaml(&revision);
    unsigned saved_commits = commits;
    assert(call(2, NULL, 0) == ESP_ERR_INVALID_STATE);
    assert(send_chunk("bad", 3, 0, 0) == ESP_ERR_INVALID_SIZE);
    assert(send_chunk("bad", 3, 1, 1) == ESP_ERR_INVALID_STATE);
    uint8_t beyond_limit[5] = {0, 0x40, 0, 0, 'x'};
    uint8_t too_large[1025] = {0};
    assert(call(1, beyond_limit, sizeof(beyond_limit)) == ESP_ERR_INVALID_SIZE);
    assert(call(1, too_large, sizeof(too_large)) == ESP_ERR_INVALID_SIZE);
    assert(call(2, beyond_limit, 1) == ESP_ERR_INVALID_SIZE);
    assert(call(3, beyond_limit, 1) == ESP_ERR_INVALID_SIZE);
    assert(!scheduled_restarts);

    char yaml[2048];
    yaml_config(yaml, sizeof(yaml), "", "");
    assert(send_document(yaml) == ESP_ERR_INVALID_ARG);
    assert_unchanged(initial, revision, saved_commits);
    assert(call(2, NULL, 0) == ESP_ERR_INVALID_STATE);

    yaml_config(yaml, sizeof(yaml), "Lab", "");
    size_t size = strlen(yaml);
    yaml[size++] = '#';
    memset(yaml + size, 'x', 1100);
    size += 1100;
    yaml[size++] = '\n';
    yaml[size] = 0;
    assert(send_chunk(yaml, size, 0, 1020) == ESP_OK);
    assert(send_chunk(yaml, size, 1021, size - 1021) == ESP_ERR_INVALID_ARG);
    assert(send_chunk(yaml, size, 1020, size - 1020) == ESP_OK);
    assert(call(2, NULL, 0) == ESP_OK);
    assert(commits == saved_commits + 1);
    assert(curio_app.network.wifi_count == 1 && !strcmp(curio_app.network.wifi[0].ssid, "Lab"));
    assert(!strcmp(curio_app.tls.certificate, "generated-cert"));
    assert(call(2, NULL, 0) == ESP_ERR_INVALID_STATE);
    free(initial);

    assert(curio_settings_init() == ESP_OK);
    assert(curio_app.configured);
    yaml_config(yaml, sizeof(yaml), "Other", "");
    assert(send_chunk(yaml, strlen(yaml), 0, strlen(yaml)) == ESP_ERR_INVALID_STATE);
    curio_app.setup_active = true;
    char *current = curio_settings_yaml(&revision);
    saved_commits = commits;
    yaml_config(yaml, sizeof(yaml), "Other", "tls:\n  certificate: bad-cert\n  private_key: key\n");
    assert(send_document(yaml) == ESP_ERR_INVALID_ARG);
    assert_unchanged(current, revision, saved_commits);
    assert(!scheduled_restarts);
    fail_write = true;
    yaml_config(yaml, sizeof(yaml), "Other", "");
    assert(send_document(yaml) == ESP_ERR_INVALID_STATE);
    assert_unchanged(current, revision, saved_commits);
    fail_write = false;
    assert(send_chunk(yaml, strlen(yaml), 0, strlen(yaml)) == ESP_OK);
    assert(call(3, NULL, 0) == ESP_OK);
    assert(planned_restarts == 1 && scheduled_restarts == 1);
    assert(call(2, NULL, 0) == ESP_ERR_INVALID_STATE);
    free(current);
    curio_app.setup_active = false;
    assert(call(3, NULL, 0) == ESP_ERR_INVALID_STATE);
    curio_alerts_destroy(curio_app.alerts);
    curio_alert_config_free(&curio_app.alert_config);
    curio_layout_free(&curio_app.layout);
    cJSON_free(document);
    puts("Iris provisioning: chunk order, commit, persistence, setup gate and restart passed");
}
