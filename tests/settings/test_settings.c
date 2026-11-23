#include "../../firmware/main/curio_settings.c"
#include <assert.h>
#include <pthread.h>

curio_app_t curio_app;
static uint8_t stored[2][CURIO_CONFIG_MAX_BYTES + 5];
static size_t stored_size[2];
static unsigned commits;
static bool fail_write;
static pthread_mutex_t app_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    memcpy(stored[index], value, size);
    stored_size[index] = size;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle == 1); commits++; return ESP_OK; }

static curio_config_t *read_config(uint32_t *revision)
{
    char error[128];
    char *yaml = curio_settings_yaml(revision);
    curio_config_t *config = calloc(1, sizeof(*config));
    assert(yaml && config && curio_config_parse(yaml, strlen(yaml), config, error, sizeof(error)));
    assert(!strstr(yaml, "revision:"));
    free(yaml);
    return config;
}
static esp_err_t save_config(const curio_config_t *config, uint32_t revision)
{
    char error[128];
    char *yaml = curio_config_yaml(config);
    assert(yaml);
    esp_err_t result = curio_settings_import(yaml, strlen(yaml), revision, error, sizeof(error));
    cJSON_free(yaml);
    return result;
}
static void test_configuration(void)
{
    curio_app.mutex = &app_mutex;
    assert(curio_settings_set_auto_rotate(true) == ESP_ERR_INVALID_STATE);
    assert(curio_settings_init() == ESP_OK);
    assert(commits == 1);
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    assert(revision == 1 && config->device.brightness == 75 && config->alerts.volume == 35 && config->alerts.vibration_strength == 60);
    assert(!strcmp(config->device.ntp_server, "pool.ntp.org") && !strcmp(curio_app.device.ntp_server, "pool.ntp.org"));
    assert(strlen(config->access.admin_token) == 64 && strlen(config->access.collector_token) == 64);
    assert(save_config(config, revision) == ESP_OK && commits == 1);
    assert(curio_settings_set_auto_rotate(true) == ESP_OK);
    curio_app.orientation.rotation = 270;
    assert(curio_settings_set_auto_rotate(false) == ESP_OK);
    assert(!curio_app.device.auto_rotate && curio_app.device.rotation == 270);
    assert(save_config(config, revision) == ESP_ERR_INVALID_VERSION);
    free(config);
    config = read_config(&revision);
    config->device.auto_rotate = true;
    assert(save_config(config, revision) == ESP_OK);
    free(config);
    config = read_config(&revision);
    config->device.auto_rotate = false;
    config->device.rotation = 0;
    curio_app.orientation.rotation = 90;
    assert(save_config(config, revision) == ESP_OK && curio_app.device.rotation == 90);
    unsigned saved = commits;
    assert(curio_settings_set_brightness(75) == ESP_OK && commits == saved);
    fail_write = true;
    assert(curio_settings_set_brightness(42) == ESP_ERR_INVALID_STATE);
    assert(curio_app.device.brightness == 75 && commits == saved);
    fail_write = false;
    assert(curio_settings_set_brightness(42) == ESP_OK);
    assert(curio_settings_set_volume(70) == ESP_OK);
    assert(curio_settings_set_volume(101) == ESP_ERR_INVALID_ARG);
    assert(curio_settings_set_page_auto_page("agents", false) == ESP_OK);
    assert(!curio_app.layout.pages[0].widgets[0].auto_page);
    assert(curio_settings_set_page_auto_page("missing", true) == ESP_ERR_NOT_FOUND);
    assert(curio_settings_set_token(true, "new-admin-token-123456") == ESP_OK);
    assert(curio_settings_set_token(false, "new-collector-token-123456") == ESP_OK);
    assert(curio_settings_set_token(true, "short") == ESP_ERR_INVALID_ARG);
    assert(curio_settings_set_token(true, "contains whitespace") == ESP_ERR_INVALID_ARG);
    assert(curio_settings_init() == ESP_OK);
    assert(curio_app.device.brightness == 42 && curio_app.device.rotation == 90);
    assert(curio_app.alert_config.volume == 70 && !curio_app.layout.pages[0].widgets[0].auto_page);
    assert(!strcmp(curio_app.access.admin_token, "new-admin-token-123456"));
    assert(!strcmp(curio_app.access.collector_token, "new-collector-token-123456"));
    free(config);
}
static void test_page_paging(void)
{
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    curio_page_config_t *page = &config->display.pages[0];
    page->widget_count = 3;
    page->widgets[0].cols = page->widgets[0].rows = 3;
    page->widgets[0].auto_page = true;
    page->widgets[1] = page->widgets[0];
    strcpy(page->widgets[1].id, "second");
    page->widgets[1].row = 3;
    page->widgets[1].auto_page = false;
    page->widgets[2] = (curio_widget_config_t){.type = CURIO_WIDGET_METRIC, .col = 3, .cols = 1, .rows = 1};
    strcpy(page->widgets[2].id, "metric");
    strcpy(page->widgets[2].host_id, "h");
    strcpy(page->widgets[2].source_id, "system.cpu");
    strcpy(page->widgets[2].key, "usage_pct");
    assert(save_config(config, revision) == ESP_OK);
    unsigned before = commits;
    fail_write = true;
    assert(curio_settings_set_page_auto_page(page->id, true) == ESP_ERR_INVALID_STATE);
    assert(curio_app.layout.pages[0].widgets[0].auto_page && !curio_app.layout.pages[0].widgets[1].auto_page);
    fail_write = false;
    assert(curio_settings_set_page_auto_page(page->id, true) == ESP_OK && commits == before + 1);
    assert(curio_app.layout.pages[0].widgets[0].auto_page && curio_app.layout.pages[0].widgets[1].auto_page);
    assert(!curio_app.layout.pages[0].widgets[2].auto_page);
    assert(curio_settings_set_page_auto_page(page->id, true) == ESP_OK && commits == before + 1);
    assert(curio_settings_set_page_auto_page(page->id, false) == ESP_OK && commits == before + 2);
    assert(!curio_app.layout.pages[0].widgets[0].auto_page && !curio_app.layout.pages[0].widgets[1].auto_page);
    assert(curio_settings_set_page_auto_page(NULL, true) == ESP_ERR_INVALID_ARG);
    free(config);
    config = read_config(&revision);
    config->display.pages[0].widget_count = 0;
    assert(save_config(config, revision) == ESP_OK);
    before = commits;
    assert(curio_settings_set_page_auto_page(config->display.pages[0].id, true) == ESP_ERR_NOT_FOUND && commits == before);
    free(config);
}
static void test_rules_preserved(void)
{
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    config->alerts.rule_count = 1;
    curio_alert_rule_t *rule = &config->alerts.rules[0];
    *rule = (curio_alert_rule_t){.type = CURIO_ALERT_AGENT_DONE, .enabled = true, .include_idle = true, .display_s = 10, .vibrate = true, .flash = true};
    strcpy(rule->id, "finished"); strcpy(rule->host_id, "*"); strcpy(rule->source_id, "herdr");
    assert(save_config(config, revision) == ESP_OK);
    curio_alerts_t *runtime = curio_app.alerts;
    uint32_t generation = curio_alerts_revision(runtime);
    assert(runtime);
    curio_agent_t agent = {.state = CURIO_STATE_WORKING};
    strcpy(agent.id, "agent"); strcpy(agent.name, "Agent");
    curio_source_t source = {.kind = CURIO_SOURCE_AGENTS, .status = CURIO_SOURCE_OK, .has_success = true,
        .max_age_ms = CURIO_STALE_MS, .agent_count = 1, .agents = &agent};
    strcpy(source.id, "herdr");
    curio_core_t *core = calloc(1, sizeof(*core));
    assert(core);
    core->hosts[0].occupied = true;
    strcpy(core->hosts[0].host_id, "host");
    core->hosts[0].sources = &source; core->hosts[0].source_count = 1;
    curio_alerts_tick(runtime, core, 1000, true);
    agent.state = CURIO_STATE_DONE;
    curio_alerts_tick(runtime, core, 2000, true);
    const curio_alert_notification_t *notification = curio_alerts_current(runtime, 2000);
    assert(notification && notification->flash && notification->vibrate && !notification->sound);
    uint32_t notification_id = notification->id;
    assert(curio_settings_set_brightness(43) == ESP_OK && curio_app.alerts == runtime);
    assert(curio_settings_set_volume(65) == ESP_OK && curio_app.alerts == runtime);
    assert(curio_alerts_revision(runtime) == generation);
    unsigned before = commits;
    assert(curio_settings_set_flash_enabled(false) == ESP_OK && commits == before);
    fail_write = true;
    assert(curio_settings_set_flash_enabled(true) == ESP_ERR_INVALID_STATE);
    assert(!curio_app.alert_config.flash_enabled && curio_app.alerts == runtime);
    fail_write = false;
    assert(curio_settings_set_flash_enabled(true) == ESP_OK && curio_app.alerts == runtime);
    assert(curio_app.alert_config.flash_enabled && curio_alerts_revision(runtime) == generation);
    assert(curio_alerts_current(runtime, 2000)->id == notification_id && curio_alerts_current(runtime, 2000)->flash);
    assert(curio_alerts_pending(runtime) == 1 && curio_alerts_remaining_ms(runtime, 3000) == 9000);
    before = commits;
    assert(curio_settings_set_vibration_strength(60) == ESP_OK && commits == before);
    assert(curio_settings_set_vibration_strength(101) == ESP_ERR_INVALID_ARG);
    fail_write = true;
    assert(curio_settings_set_vibration_strength(88) == ESP_ERR_INVALID_STATE);
    assert(commits == before && curio_app.alert_config.vibration_strength == 60 && curio_app.alerts == runtime);
    fail_write = false;
    assert(curio_settings_set_vibration_strength(88) == ESP_OK && curio_app.alerts == runtime);
    assert(curio_app.alert_config.vibration_strength == 88 && curio_alerts_revision(runtime) == generation);
    notification = curio_alerts_current(runtime, 2000);
    assert(notification && notification->id == notification_id && notification->vibration_strength == 88);
    assert(curio_alerts_pending(runtime) == 1 && curio_alerts_remaining_ms(runtime, 3000) == 9000);
    assert(curio_settings_set_vibration_strength(0) == ESP_OK && curio_app.alerts == runtime);
    assert(curio_settings_set_vibration_strength(88) == ESP_OK && curio_app.alerts == runtime);
    free(config);
    config = read_config(&revision);
    assert(config->alerts.vibration_strength == 88 && config->alerts.rules[0].vibrate && !config->alerts.rules[0].sound);
    assert(config->alerts.flash_enabled && config->alerts.rules[0].flash);
    config->alerts.flash_enabled = false;
    config->alerts.volume = 60;
    assert(save_config(config, revision) == ESP_OK && curio_app.alerts == runtime);
    free(config);
    assert(!curio_app.alert_config.flash_enabled && curio_alerts_current(runtime, 2000)->flash);
    curio_alerts_dismiss(runtime, 2000);
    curio_alerts_tick(runtime, core, 3000, true);
    assert(!curio_alerts_pending(runtime));
    free(core);
    assert(curio_settings_init() == ESP_OK);
    assert(curio_app.alert_config.vibration_strength == 88 && curio_app.alert_config.rules[0].vibrate);
}
typedef struct { char *yaml; uint32_t revision; esp_err_t result; } save_t;
static void *save_thread(void *arg)
{
    save_t *save = arg;
    char error[128];
    save->result = curio_settings_import(save->yaml, strlen(save->yaml), save->revision, error, sizeof(error));
    return NULL;
}
static void test_concurrent_saves(void)
{
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    pthread_t threads[2];
    save_t saves[2];
    for (unsigned i = 0; i < 2; i++) {
        config->device.brightness = 30 + i;
        saves[i] = (save_t){.yaml = curio_config_yaml(config), .revision = revision};
    }
    unsigned before = commits;
    for (unsigned i = 0; i < 2; i++) assert(!pthread_create(&threads[i], NULL, save_thread, &saves[i]));
    for (unsigned i = 0; i < 2; i++) assert(!pthread_join(threads[i], NULL));
    assert(commits == before + 1);
    assert((saves[0].result == ESP_OK && saves[1].result == ESP_ERR_INVALID_VERSION) ||
           (saves[1].result == ESP_OK && saves[0].result == ESP_ERR_INVALID_VERSION));
    for (unsigned i = 0; i < 2; i++) cJSON_free(saves[i].yaml);
    free(config);
}
int main(void)
{
    test_configuration();
    test_page_paging();
    test_rules_preserved();
    test_concurrent_saves();
    curio_alerts_destroy(curio_app.alerts);
    cJSON_free(document);
    puts("YAML settings: persistence, failures, tokens, local edits, runtime preservation and concurrent saves passed");
}
