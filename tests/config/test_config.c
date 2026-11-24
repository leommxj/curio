#include "curio_config_model.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *minimal =
    "access:\n  admin_token: example-admin-token-0001\n  collector_token: example-collector-token-0001\n"
    "screen:\n  pages:\n    - id: home\n      widgets:\n        - type: agents\n          at: [0, 0]\n          size: [4, 6]\n";
static curio_config_t config;
static unsigned allocations, fail_at;
static void *limited_alloc(size_t size) { return ++allocations == fail_at ? NULL : malloc(size); }


static void test_defaults(void)
{
    char error[80];
    assert(curio_config_parse(minimal, strlen(minimal), &config, error, sizeof(error)) && !*error);
    assert(config.device.brightness == 75 && config.device.rotation == 0 && !config.device.auto_rotate);
    assert(!*config.network.ssid && !*config.network.password && !strcmp(config.network.hostname, "curio"));
    assert(!*config.tls.certificate && !*config.tls.private_key);
    assert(config.display.page_count == 1 && !strcmp(config.display.default_page, "home"));
    curio_widget_config_t *w = &config.display.pages[0].widgets[0];
    assert(!strcmp(w->id, "agents-1") && !strcmp(w->title, "Agents") && w->col == 0 && w->row == 0 && w->cols == 4 && w->rows == 6);
    assert(w->all_hosts && w->auto_page && !strcmp(w->source_id, "herdr") && w->filter == CURIO_FILTER_ALL);
    assert(config.alerts.volume == 35 && config.alerts.vibration_strength == 60 && !config.alerts.rule_count && !config.alerts.flash_enabled);
    char *yaml = curio_config_yaml(&config);
    assert(yaml && !strstr(yaml, "revision") && strstr(yaml, "admin_token:") && strstr(yaml, "vibration_strength: 60") && strstr(yaml, "flash_enabled: false") && !strstr(yaml, "flash:") && !strstr(yaml, "vibrate:"));
    assert(strstr(yaml, "at: [0, 0]") && strstr(yaml, "size: [4, 6]") && !strstr(yaml, "slot:") && !strstr(yaml, "layout:"));
    assert(!strstr(yaml, "auto_page:") && !strstr(yaml, "source:") && !strstr(yaml, "hosts:"));
    assert(!strstr(yaml, "network:") && !strstr(yaml, "tls:"));
    assert(curio_config_parse(yaml, strlen(yaml), &config, error, sizeof(error)));
    cJSON_free(yaml);
}
static void test_roundtrip(void)
{
    const char *yaml =
      "version: 1\naccess: {admin_token: '1234567890123456', collector_token: abcdefghijklmnop}\n"
      "device: {brightness: 0, rotation: 270, auto_rotate: true}\n"
      "screen:\n  default_page: stats\n  pages:\n"
      "  - id: home\n    title: '主页: 状态'\n    widgets:\n"
      "    - {id: team, type: agents, at: [1, 2], size: [3, 4], source: custom, hosts: [one, two], filter: attention, auto_page: false, project: 'true', agent: '123'}\n"
      "  - id: stats\n    widgets:\n"
      "    - {type: metric, at: [3, 5], size: [1, 1], host: one, source: system.memory, key: usage_pct, unit: percent}\n"
      "    - {type: machine, at: [0, 0], size: [2, 2], host: one}\n"
      "    - {type: status, at: [2, 0], size: [2, 2], host: one, source: service.api}\n"
      "alerts:\n  volume: 0\n  vibration_strength: 0\n  flash_enabled: true\n  rules:\n"
      "  - {type: agent_done, duration: 0, flash: true, vibrate: true, sound: false, include_idle: false}\n"
      "  - {type: metric_high, host: one, source: system.memory, key: usage_pct, unit: percent, threshold: 90, recover: 80}\n"
      "  - {type: host_offline, enabled: false}\n";
    assert(curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
    assert(config.alerts.rules[0].display_s == 0 && config.alerts.rules[0].vibrate && !config.alerts.rules[0].sound && !config.alerts.rules[0].include_idle && config.alerts.vibration_strength == 0);
    assert(config.alerts.flash_enabled && config.alerts.rules[0].flash && !config.alerts.rules[1].flash);
    assert(config.alerts.rules[1].hold_s == 60 && config.alerts.rules[1].display_s == 10);
    assert(config.alerts.rules[2].hold_s == 30 && !config.alerts.rules[2].enabled);
    const curio_widget_config_t *status = &config.display.pages[1].widgets[2];
    assert(status->type == CURIO_WIDGET_STATUS && !strcmp(status->title, "Status"));
    assert(!strcmp(status->host_id, "one") && !strcmp(status->source_id, "service.api"));
    curio_config_t *before = malloc(sizeof(*before));
    assert(before);
    *before = config;
    char *emitted = curio_config_yaml(&config);
    assert(emitted && strlen(emitted) < CURIO_CONFIG_MAX_BYTES);
    assert(curio_config_parse(emitted, strlen(emitted), &config, NULL, 0));
    assert(!memcmp(before, &config, sizeof(config)));
    assert(strstr(emitted, "duration: 0") && !strstr(emitted, "duration: 10"));
    cJSON_free(emitted);
    free(before);
}
static void reject(const char *suffix)
{
    size_t n = strlen(minimal) + strlen(suffix);
    char *yaml = malloc(n + 1);
    assert(yaml);
    strcpy(yaml, minimal); strcat(yaml, suffix);
    curio_config_t *before = malloc(sizeof(*before));
    assert(before); *before = config;
    char error[80];
    assert(!curio_config_parse(yaml, n, &config, error, sizeof(error)) && *error);
    assert(!memcmp(before, &config, sizeof(config)));
    free(before); free(yaml);
}
static void test_invalid(void)
{
    const char *bad[] = {
      "alerts: {flash_enabled: 1}\n", "alerts: {flash_enabled: 'true'}\n", "alerts: {flash_enabled: null}\n",
      "alerts: {rules: [{type: agent_done, flash: 1}]}\n", "alerts: {rules: [{type: host_offline, flash: 'true'}]}\n",
      "alerts: {rules: [{type: metric_high, host: h, source: s, key: k, unit: percent, threshold: 90, recover: 80, flash: null}]}\n",
      "unknown: 1\n", "version: 2\n", "version: 1\nversion: 1\n", "device: {brightness: 101}\n",
      "device: {rotation: 45}\n", "device: {auto_rotate: yes}\n", "device: {rotation: '90'}\n",
      "device: {brightness: 50, brightness: 60}\n", "device: {extra: 0}\n",
      "alerts: {rules: [{type: metric_high}]}\n", "alerts: {rules: [{type: host_offline, duration: -1}]}\n",
      "alerts: {rules: [{type: host_offline, hold: 4}]}\n", "alerts: {rules: [{type: agent_done, extra: true}]}\n",
      "device: &shared {brightness: 50}\n", "device: !!map {brightness: 50}\n", "device: *shared\n",
      "---\nother: document\n", "alerts: {volume: .inf}\n", "alerts: {volume: null}\n", "alerts: {vibration_strength: 101}\n", "alerts: {vibration_strength: -1}\n", "alerts: {vibration_strength: 1.5}\n", "alerts: {vibration_strength: '60'}\n", "alerts: {vibration_strength: true}\n", "alerts: {rules: [{type: agent_done, vibrate: 1}]}\n", "alerts: {rules: [{type: host_offline, vibrate: 'true'}]}\n", "alerts: {rules: [{type: metric_high, host: h, source: s, key: k, unit: percent, threshold: 90, recover: 80, vibrate: null}]}\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) reject(bad[i]);
    assert(!curio_config_parse(minimal, CURIO_CONFIG_MAX_BYTES + 1, &config, NULL, 0));
    const char *invalid[] = {
      "access: {admin_token: short, collector_token: abcdefghijklmnop}\nscreen: {pages: []}\n",
      "access: {admin_token: 'abcdefgh ijklmnop', collector_token: abcdefghijklmnop}\nscreen: {pages: []}\n",
      "access: {admin_token: abcdefghijklmnop, collector_token: abcdefghijklmnop}\nscreen: {pages: [{id: p, widgets: [{type: machine}]}]}\n",
      "access: {admin_token: abcdefghijklmnop, collector_token: abcdefghijklmnop}\nscreen: {pages: [{id: p, widgets: [{type: agents, unknown: true}]}]}\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) assert(!curio_config_parse(invalid[i], strlen(invalid[i]), &config, NULL, 0));
}
static void test_network_tls(void)
{
    const char *yaml =
        "network: {ssid: '工作室 WiFi', password: 'password123', hostname: curio-desk}\n"
        "tls:\n"
        "  certificate: |\n"
        "    -----BEGIN CERTIFICATE-----\n"
        "    example-certificate\n"
        "    -----END CERTIFICATE-----\n"
        "  private_key: |\n"
        "    -----BEGIN PRIVATE KEY-----\n"
        "    example-key\n"
        "    -----END PRIVATE KEY-----\n";
    size_t n = strlen(minimal) + strlen(yaml) + 1;
    char *input = malloc(n);
    assert(input);
    strcpy(input, minimal);
    strcat(input, yaml);
    assert(curio_config_parse(input, strlen(input), &config, NULL, 0));
    assert(!strcmp(config.network.ssid, "工作室 WiFi") && !strcmp(config.network.password, "password123"));
    assert(!strcmp(config.network.hostname, "curio-desk"));
    assert(strstr(config.tls.certificate, "example-certificate\n"));
    curio_config_t *before = calloc(1, sizeof(*before));
    assert(before);
    *before = config;
    char *out = curio_config_yaml(&config);
    assert(out && strstr(out, "certificate: |") && strstr(out, "private_key: |"));
    assert(curio_config_parse(out, strlen(out), &config, NULL, 0));
    assert(!memcmp(before, &config, sizeof(config)));
    cJSON_free(out);
    free(before);
    free(input);

    const char *invalid[] = {
        "network: null\n", "network: {ssid: true}\n", "network: {ssid: 123}\n",
        "network: {ssid: abc, password: short}\n", "network: {hostname: ''}\n",
        "network: {hostname: -curio}\n", "network: {hostname: curio-}\n",
        "network: {hostname: curio.local}\n", "network: {hostname: curio_name}\n",
        "network: {extra: value}\n", "tls: null\n", "tls: {}\n",
        "tls: {certificate: cert}\n", "tls: {private_key: key}\n",
        "tls: {certificate: '', private_key: key}\n",
        "tls: {certificate: cert, private_key: ''}\n",
        "tls: {certificate: cert, private_key: key, extra: x}\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) reject(invalid[i]);

    char suffix[2300];
    char value[2100];
    memset(value, 'a', sizeof(value));
    value[32] = 0;
    snprintf(suffix, sizeof(suffix), "network: {ssid: '%s'}\n", value);
    n = strlen(minimal) + strlen(suffix) + 1;
    input = malloc(n); assert(input);
    strcpy(input, minimal); strcat(input, suffix);
    assert(curio_config_parse(input, strlen(input), &config, NULL, 0));
    free(input);
    value[32] = 'a'; value[33] = 0;
    snprintf(suffix, sizeof(suffix), "network: {ssid: '%s'}\n", value);
    reject(suffix);
    memset(value, 'a', sizeof(value));
    value[63] = 0;
    snprintf(suffix, sizeof(suffix), "network: {ssid: radio, password: '%s'}\n", value);
    n = strlen(minimal) + strlen(suffix) + 1;
    input = malloc(n); assert(input);
    strcpy(input, minimal); strcat(input, suffix);
    assert(curio_config_parse(input, strlen(input), &config, NULL, 0));
    free(input);
    value[63] = 'a'; value[64] = 0;
    snprintf(suffix, sizeof(suffix), "network: {ssid: radio, password: '%s'}\n", value);
    reject(suffix);
    memset(value, 'a', sizeof(value)); value[2048] = 0;
    snprintf(suffix, sizeof(suffix), "tls: {certificate: '%s', private_key: key}\n", value);
    reject(suffix);
}
static void test_grid_fields(void)
{
    const char *invalid_widgets[] = {
        "type: agents, size: [4, 6]",
        "type: agents, at: [0, 0]",
        "type: agents, at: [0, 0], size: [4, 6], slot: 2",
        "type: agents, at: [0], size: [4, 6]",
        "type: agents, at: [0, 0, 0], size: [4, 6]",
        "type: agents, at: [0, 0], size: [4]",
        "type: agents, at: [0, 0], size: [4, 6, 1]",
        "type: agents, at: [0, '0'], size: [4, 6]",
        "type: agents, at: [false, 0], size: [4, 6]",
        "type: agents, at: [null, 0], size: [4, 6]",
        "type: agents, at: [0.5, 0], size: [3, 3]",
        "type: agents, at: [-1, 0], size: [3, 3]",
        "type: agents, at: [4294967295, 0], size: [3, 3]",
        "type: agents, at: [0, 0], size: [3, 3.5]",
        "type: agents, at: [0, 0], size: [3, '3']",
        "type: agents, at: [0, 0], size: [0, 0]",
        "type: agents, at: [0, 0], size: [2, 3]",
        "type: agents, at: [0, 0], size: [3, 2]",
        "type: agents, at: [2, 0], size: [3, 3]",
        "type: agents, at: [0, 4], size: [3, 3]",
        "type: agents, at: [0, 0], size: [5, 6]",
        "type: machine, host: one, at: [0, 0], size: [1, 2]",
        "type: machine, host: one, at: [0, 0], size: [2, 1]",
        "type: metric, host: one, source: system.cpu, key: usage_pct, at: [0, 0], size: [0, 1]",
        "type: status, host: one, at: [0, 0], size: [1, 1]",
        "type: status, source: service.api, at: [0, 0], size: [1, 1]",
        "type: status, host: one, source: service.api, key: state, at: [0, 0], size: [1, 1]",
        "type: status, host: one, source: service.api, unit: number, at: [0, 0], size: [1, 1]",
    };
    char yaml[2048];
    const char *access = "access: {admin_token: abcdefghijklmnop, collector_token: abcdefghijklmnop}\n";
    for (size_t i = 0; i < sizeof(invalid_widgets) / sizeof(*invalid_widgets); i++) {
        snprintf(yaml, sizeof(yaml), "%sscreen: {pages: [{id: home, widgets: [{%s}]}]}\n", access, invalid_widgets[i]);
        assert(!curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
    }
    const char *invalid_pages[] = {
        "{id: home, layout: focus, widgets: []}",
        "{id: home, layout: grid, widgets: []}",
        "{id: home, widgets: [{type: agents, at: [0, 0], size: [3, 3]}, {type: machine, host: one, at: [2, 2], size: [2, 2]}]}",
        "{id: home, widgets: [{id: same, type: agents, at: [0, 0], size: [3, 3]}, {id: same, type: agents, at: [0, 3], size: [3, 3]}]}",
    };
    for (size_t i = 0; i < sizeof(invalid_pages) / sizeof(*invalid_pages); i++) {
        snprintf(yaml, sizeof(yaml), "%sscreen: {pages: [%s]}\n", access, invalid_pages[i]);
        assert(!curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
    }
    snprintf(yaml, sizeof(yaml), "%sscreen: {pages: [{id: home, widgets: ["
        "{id: upper, type: agents, at: [0, 0], size: [3, 3]},"
        "{id: lower, type: agents, at: [0, 3], size: [3, 3]},"
        "{type: metric, host: one, source: system.cpu, key: usage_pct, at: [3, 0], size: [1, 6]}]}]}\n", access);
    assert(curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
    char *emitted = curio_config_yaml(&config);
    assert(emitted && strstr(emitted, "at: [0, 3]") && strstr(emitted, "size: [1, 6]"));
    assert(curio_config_parse(emitted, strlen(emitted), &config, NULL, 0));
    cJSON_free(emitted);
}
static void test_allocation_failure(void)
{
    cJSON_Hooks hooks = {limited_alloc, free};
    for (unsigned i = 1; i <= 50; i++) {
        allocations = 0; fail_at = i;
        cJSON_InitHooks(&hooks);
        (void)curio_config_parse(minimal, strlen(minimal), &config, NULL, 0);
        cJSON_InitHooks(NULL);
    }
    assert(curio_config_parse(minimal, strlen(minimal), &config, NULL, 0));
    for (unsigned i = 1; i <= 50; i++) {
        allocations = 0; fail_at = i;
        cJSON_InitHooks(&hooks);
        char *yaml = curio_config_yaml(&config);
        cJSON_free(yaml);
        cJSON_InitHooks(NULL);
    }
}

static void test_clock(void)
{
    const char *prefix = "access: {admin_token: '1234567890123456', collector_token: abcdefghijklmnop}\nscreen: {pages: [{id: home, widgets: [{type: clock, at: [0, 0], size: [1, 1]";
    const char *valid[] = {"", ", utc_offset: '+14:00', seconds: true", ", utc_offset: '-12:00'", ", utc_offset: '+05:45'", ", utc_offset: '-03:17'"};
    char yaml[1024];
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        snprintf(yaml, sizeof(yaml), "%s%s}]}]}\n", prefix, valid[i]);
        assert(curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
        curio_widget_config_t *w = &config.display.pages[0].widgets[0];
        assert(w->type == CURIO_WIDGET_CLOCK && !*w->host_id && !*w->source_id);
        assert(!*w->title);
        assert(!strcmp(config.device.ntp_server, "pool.ntp.org"));
        char *out = curio_config_yaml(&config);
        assert(out && !strstr(out, "host:") && !strstr(out, "source:") && !strstr(out, "ntp_server:") && !strstr(out, "title:"));
        if (!i) assert(!strstr(out, "utc_offset:") && !strstr(out, "seconds:"));
        int offset = w->utc_offset_minutes;
        assert(curio_config_parse(out, strlen(out), &config, NULL, 0));
        assert(config.display.pages[0].widgets[0].utc_offset_minutes == offset);
        cJSON_free(out);
    }
    strcpy(config.display.pages[0].widgets[0].title, "Office");
    char *custom = curio_config_yaml(&config);
    assert(custom && curio_config_parse(custom, strlen(custom), &config, NULL, 0));
    assert(!strcmp(config.display.pages[0].widgets[0].title, "Office"));
    cJSON_free(custom);
    const char *invalid[] = {", utc_offset: '+14:01'", ", utc_offset: '-12:01'", ", utc_offset: '08:00'", ", utc_offset: '+8:00'", ", utc_offset: '+00:60'", ", utc_offset: 480", ", seconds: 1", ", host: one", ", source: time"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        snprintf(yaml, sizeof(yaml), "%s%s}]}]}\n", prefix, invalid[i]);
        assert(!curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
    }
    snprintf(yaml, sizeof(yaml), "%s}]}]}\ndevice: {ntp_server: time.example.com}\n", prefix);
    assert(curio_config_parse(yaml, strlen(yaml), &config, NULL, 0));
    char *out = curio_config_yaml(&config);
    assert(out && strstr(out, "ntp_server: time.example.com"));
    cJSON_free(out);
    const char *servers[] = {"192.168.1.1", "2001:db8::1", "", "http://host", "some host", " host", "时间.example"};
    for (size_t i = 0; i < sizeof(servers) / sizeof(*servers); i++) {
        snprintf(yaml, sizeof(yaml), "%s}]}]}\ndevice: {ntp_server: '%s'}\n", prefix, servers[i]);
        assert(curio_config_parse(yaml, strlen(yaml), &config, NULL, 0) == (i < 2));
        strcpy(config.device.ntp_server, servers[i]);
        out = curio_config_yaml(&config);
        assert((out != NULL) == (i < 2));
        cJSON_free(out);
    }
}

int main(void)
{
    test_clock(); test_defaults(); test_roundtrip(); test_invalid(); test_network_tls(); test_grid_fields(); test_allocation_failure();
    puts("config tests passed");
}
