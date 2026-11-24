#include "../../firmware/main/curio_settings.c"
#include "../../firmware/modules/widgets/agents/config.h"
#include "fixture.h"

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
    assert(curio_app.import_shared_wifi);
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
    curio_config_free(config);
    free(config);
    config = read_config(&revision);
    config->device.auto_rotate = true;
    assert(save_config(config, revision) == ESP_OK);
    curio_config_free(config);
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
    assert(!curio_app.layout.pages[0].widgets[0].type->paging(curio_app.layout.pages[0].widgets[0].options));
    assert(curio_settings_set_page_auto_page("missing", true) == ESP_ERR_NOT_FOUND);
    assert(curio_settings_set_token(true, "new-admin-token-123456") == ESP_OK);
    assert(curio_settings_set_token(false, "new-collector-token-123456") == ESP_OK);
    assert(curio_settings_set_token(true, "short") == ESP_ERR_INVALID_ARG);
    assert(curio_settings_set_token(true, "contains whitespace") == ESP_ERR_INVALID_ARG);
    assert(curio_settings_init() == ESP_OK);
    assert(!curio_app.import_shared_wifi);
    assert(curio_app.device.brightness == 42 && curio_app.device.rotation == 90);
    assert(curio_app.alert_config.volume == 70 && !curio_app.layout.pages[0].widgets[0].type->paging(curio_app.layout.pages[0].widgets[0].options));
    assert(!strcmp(curio_app.access.admin_token, "new-admin-token-123456"));
    assert(!strcmp(curio_app.access.collector_token, "new-collector-token-123456"));
    curio_config_free(config);
    free(config);
}
static void test_network_tls(void)
{
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    assert(!config->network.wifi_count && !*config->tls.certificate && !strcmp(config->network.hostname, "curio"));
    config->network.wifi_count = 2;
    strcpy(config->network.wifi[0].ssid, "Studio WiFi");
    strcpy(config->network.wifi[0].password, "password123");
    strcpy(config->network.wifi[1].ssid, "Studio Guest");
    strcpy(config->network.hostname, "curio-studio");
    strcpy(config->tls.certificate, "supplied-cert");
    strcpy(config->tls.private_key, "supplied-key");
    unsigned prepared = tls_prepares;
    assert(save_config(config, revision) == ESP_OK && tls_prepares == prepared + 1);
    assert(!curio_app.configured);
    curio_config_free(config);
    free(config);
    config = read_config(&revision);
    assert(config->network.wifi_count == 2 && !strcmp(config->network.wifi[0].ssid, "Studio WiFi") &&
           !strcmp(config->network.wifi[1].ssid, "Studio Guest") &&
           !strcmp(config->tls.certificate, "supplied-cert"));
    assert(!strcmp(curio_app.network.hostname, "curio-studio") && !strcmp(curio_app.tls.private_key, "supplied-key"));
    prepared = tls_prepares;
    assert(save_config(config, revision) == ESP_OK && tls_prepares == prepared);
    assert(curio_settings_set_brightness(44) == ESP_OK && tls_prepares == prepared);
    assert(curio_settings_set_token(true, "another-admin-token-123456") == ESP_OK && tls_prepares == prepared);
    assert(curio_settings_init() == ESP_OK && tls_prepares == prepared);
    assert(curio_app.configured);
    assert(curio_app.network.wifi_count == 2 && !strcmp(curio_app.network.wifi[0].ssid, "Studio WiFi") &&
           !strcmp(curio_app.network.wifi[1].ssid, "Studio Guest") &&
           !strcmp(curio_app.tls.certificate, "supplied-cert"));
    curio_config_free(config);
    free(config);
    config = read_config(&revision);
    assert(!strcmp(config->network.wifi[0].password, "password123") && !strcmp(config->tls.private_key, "supplied-key"));
    strcpy(config->network.hostname, "curio-renamed");
    memset(&config->tls, 0, sizeof(config->tls));
    assert(save_config(config, revision) == ESP_OK && tls_prepares == prepared + 1);
    curio_config_free(config);
    free(config);
    config = read_config(&revision);
    assert(!strcmp(config->network.hostname, "curio-renamed"));
    assert(!strcmp(config->tls.certificate, "generated-cert:curio-renamed"));
    assert(!strcmp(config->tls.private_key, "generated-key"));

    char *saved_yaml = curio_settings_yaml(NULL);
    assert(saved_yaml);
    unsigned saved_commits = commits;
    strcpy(config->tls.certificate, "bad-cert");
    strcpy(config->tls.private_key, "bad-key");
    assert(save_config(config, revision) == ESP_ERR_INVALID_ARG && tls_prepares == prepared + 2);
    assert(commits == saved_commits && !strcmp(curio_app.tls.certificate, "generated-cert:curio-renamed"));
    char *after_yaml = curio_settings_yaml(NULL);
    assert(after_yaml && !strcmp(saved_yaml, after_yaml));
    free(saved_yaml);
    free(after_yaml);
    curio_config_free(config);
    free(config);
}
static void test_page_paging(void)
{
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    curio_page_config_t *page = &config->display.pages[0];
    page->widget_count = 3;
    page->widgets[0].cols = page->widgets[0].rows = 3;
    page->widgets[0].type->set_paging(page->widgets[0].options,true);
    curio_widget_config_free(&page->widgets[1]);
    assert(curio_widget_config_clone(&page->widgets[1],&page->widgets[0]));
    strcpy(page->widgets[1].id, "second");
    page->widgets[1].row = 3;
    page->widgets[1].type->set_paging(page->widgets[1].options,false);
    curio_widget_config_free(&page->widgets[2]);
    page->widgets[2] = (curio_widget_config_t){.type = curio_widget_type_find("metric"), .col = 3, .cols = 1, .rows = 1};
    strcpy(page->widgets[2].id,"metric");
    page->widgets[2].options = cJSON_malloc(page->widgets[2].type->options_size);
    cJSON *metric = cJSON_Parse("{\"host\":\"h\",\"source\":\"system.cpu\",\"key\":\"usage_pct\"}");
    assert(metric && page->widgets[2].options && page->widgets[2].type->parse(metric,page->widgets[2].options));
    cJSON_Delete(metric);
    assert(save_config(config, revision) == ESP_OK);
    unsigned before = commits;
    fail_write = true;
    assert(curio_settings_set_page_auto_page(page->id, true) == ESP_ERR_INVALID_STATE);
    assert(curio_app.layout.pages[0].widgets[0].type->paging(curio_app.layout.pages[0].widgets[0].options) && !curio_app.layout.pages[0].widgets[1].type->paging(curio_app.layout.pages[0].widgets[1].options));
    fail_write = false;
    assert(curio_settings_set_page_auto_page(page->id, true) == ESP_OK && commits == before + 1);
    assert(curio_app.layout.pages[0].widgets[0].type->paging(curio_app.layout.pages[0].widgets[0].options) && curio_app.layout.pages[0].widgets[1].type->paging(curio_app.layout.pages[0].widgets[1].options));
    assert(!curio_app.layout.pages[0].widgets[2].type->paging || !curio_app.layout.pages[0].widgets[2].type->paging(curio_app.layout.pages[0].widgets[2].options));
    assert(curio_settings_set_page_auto_page(page->id, true) == ESP_OK && commits == before + 1);
    assert(curio_settings_set_page_auto_page(page->id, false) == ESP_OK && commits == before + 2);
    assert(!curio_app.layout.pages[0].widgets[0].type->paging(curio_app.layout.pages[0].widgets[0].options) && !curio_app.layout.pages[0].widgets[1].type->paging(curio_app.layout.pages[0].widgets[1].options));
    assert(curio_settings_set_page_auto_page(NULL, true) == ESP_ERR_INVALID_ARG);
    curio_config_free(config);
    free(config);
    config = read_config(&revision);
    for (size_t i = 0; i < config->display.pages[0].widget_count; i++) curio_widget_config_free(&config->display.pages[0].widgets[i]);
    config->display.pages[0].widget_count = 0;
    assert(save_config(config, revision) == ESP_OK);
    before = commits;
    assert(curio_settings_set_page_auto_page(config->display.pages[0].id, true) == ESP_ERR_NOT_FOUND && commits == before);
    curio_config_free(config);
    free(config);
}
static void test_rules_preserved(void)
{
    uint32_t revision;
    curio_config_t *config = read_config(&revision);
    config->alerts.rule_count = 1;
    curio_alert_rule_t *rule = &config->alerts.rules[0];
    *rule = (curio_alert_rule_t){.type = curio_alert_type_find("agent_done"), .enabled = true, .display_s = 10, .vibrate = true, .flash = true};
    strcpy(rule->id,"finished");
    rule->options = cJSON_malloc(rule->type->options_size);
    cJSON *options = cJSON_Parse("{\"host\":\"*\",\"source\":\"herdr\",\"include_idle\":true}");
    assert(options && rule->options && rule->type->parse(options,rule->options));
    cJSON_Delete(options);
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
    curio_config_free(config);
    free(config);
    config = read_config(&revision);
    assert(config->alerts.vibration_strength == 88 && config->alerts.rules[0].vibrate && !config->alerts.rules[0].sound);
    assert(config->alerts.flash_enabled && config->alerts.rules[0].flash);
    config->alerts.flash_enabled = false;
    config->alerts.volume = 60;
    assert(save_config(config, revision) == ESP_OK && curio_app.alerts == runtime);
    curio_config_free(config);
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
    curio_config_free(config);
    free(config);
}

static void test_adopt_wifi(void)
{
    stored_size[0] = stored_size[1] = 0;
    assert(curio_settings_init() == ESP_OK);
    assert(curio_settings_set_brightness(41) == ESP_OK);
    assert(curio_settings_set_volume(67) == ESP_OK);
    assert(curio_settings_set_page_auto_page("agents", false) == ESP_OK);
    assert(curio_settings_set_token(true, "adopted-admin-token-123456") == ESP_OK);
    assert(curio_settings_set_token(false, "adopted-collector-token-123456") == ESP_OK);
    curio_network_config_t network = {.hostname = "curio", .wifi_count = 1,
        .wifi = {{.ssid = "Shared Home", .password = "shared-password"}}};
    uint32_t revision;
    char *before = curio_settings_yaml(&revision);
    unsigned saved_commits = commits;
    fail_write = true;
    assert(curio_settings_adopt_wifi(&network) == ESP_ERR_INVALID_STATE);
    fail_write = false;
    char *after = curio_settings_yaml(NULL);
    assert(after && before && !strcmp(before, after) && commits == saved_commits);
    assert(!curio_app.network.wifi_count && !*curio_app.tls.certificate);
    free(before);
    free(after);

    assert(curio_settings_adopt_wifi(&network) == ESP_OK);
    assert(commits == saved_commits + 1);
    curio_config_t *config = read_config(&revision);
    assert(config->network.wifi_count == 1 && !strcmp(config->network.wifi[0].ssid, "Shared Home"));
    assert(!strcmp(config->network.wifi[0].password, "shared-password"));
    assert(!strcmp(config->tls.certificate, "generated-cert:curio"));
    assert(!strcmp(config->access.admin_token, "adopted-admin-token-123456"));
    assert(!strcmp(config->access.collector_token, "adopted-collector-token-123456"));
    assert(config->device.brightness == 41 && config->alerts.volume == 67);
    assert(!((curio_agents_config_t *)config->display.pages[0].widgets[0].options)->auto_page);
    curio_config_free(config);
    free(config);

    strcpy(network.wifi[0].ssid, "Replacement");
    before = curio_settings_yaml(&revision);
    saved_commits = commits;
    assert(curio_settings_adopt_wifi(&network) == ESP_ERR_INVALID_STATE);
    after = curio_settings_yaml(NULL);
    assert(before && after && !strcmp(before, after) && commits == saved_commits);
    assert(!strcmp(curio_app.network.wifi[0].ssid, "Shared Home"));
    free(before);
    free(after);
    assert(curio_settings_init() == ESP_OK && curio_app.configured);
    assert(!strcmp(curio_app.network.wifi[0].ssid, "Shared Home"));
    assert(!strcmp(curio_app.access.admin_token, "adopted-admin-token-123456"));
}
static void test_reset(void)
{
    assert(curio_settings_set_auto_rotate(true) == ESP_OK);
    curio_app.orientation.rotation = 270;
    char *before = curio_settings_yaml(NULL);
    curio_access_config_t old_access = curio_app.access;
    curio_network_config_t old_network = curio_app.network;
    assert(curio_app.alerts && curio_app.alert_config.rule_count && old_network.wifi_count);
    fail_write = true;
    assert(curio_settings_reset() == ESP_ERR_INVALID_STATE && !reset_pending);
    char *after = curio_settings_yaml(NULL);
    assert(!strcmp(before, after));
    free(after);
    fail_write = false;
    assert(curio_settings_reset() == ESP_OK && reset_pending);
    after = curio_settings_yaml(NULL);
    assert(!strcmp(before, after));
    free(before);
    free(after);
    fail_blob_slot = 0;
    assert(curio_settings_init() == ESP_ERR_INVALID_STATE && reset_pending);
    fail_blob_slot = -1;
    assert(curio_settings_init() == ESP_OK && !reset_pending);
    assert(!curio_app.configured && !curio_app.import_shared_wifi);
    assert(!curio_app.network.wifi_count && !strcmp(curio_app.network.hostname, "curio"));
    assert(!curio_app.tls.certificate[0] && !curio_app.tls.private_key[0]);
    assert(strcmp(old_access.admin_token, curio_app.access.admin_token));
    assert(strcmp(old_access.collector_token, curio_app.access.collector_token));
    assert(curio_app.device.brightness == 75 && !curio_app.device.auto_rotate && curio_app.device.rotation == 0);
    assert(curio_app.alert_config.volume == 35 && !curio_app.alert_config.rule_count && !curio_app.alerts);
    assert(curio_app.layout.page_count == 1 && curio_app.layout.pages[0].widget_count == 1);
    assert(curio_app.layout.pages[0].widgets[0].type == &curio_widget_agents);
    for (unsigned i = 0; i < 2; i++) {
        assert(stored_size[i] && !strstr((char *)stored[i] + 4, old_access.admin_token));
        assert(!strstr((char *)stored[i] + 4, old_network.wifi[0].ssid));
    }
    before = curio_settings_yaml(NULL);
    stored[0][4] = '!';
    assert(curio_settings_init() == ESP_OK && !curio_app.import_shared_wifi);
    after = curio_settings_yaml(NULL);
    assert(!strcmp(before, after));
    free(before);
    free(after);
}

int main(void)
{
    test_configuration();
    test_network_tls();
    test_page_paging();
    test_rules_preserved();
    test_concurrent_saves();
    test_reset();
    test_adopt_wifi();
    curio_alerts_destroy(curio_app.alerts);
    curio_alert_config_free(&curio_app.alert_config);
    curio_layout_free(&curio_app.layout);
    cJSON_free(document);
    puts("YAML settings: persistence, failures, Wi-Fi adoption, reset, tokens, local edits and concurrent saves passed");
}
