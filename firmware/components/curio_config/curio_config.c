#include "curio_config_model.h"
#include "cJSON.h"
#include "curio_module.h"
#include "yaml.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const cJSON *field(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }
static bool fields(const cJSON *o, const char *allowed) { return curio_fields(o, allowed, NULL); }
static cJSON *read_node(yaml_parser_t *parser, yaml_event_t *event, unsigned depth)
{
    if (depth > 12) return NULL;
    cJSON *out = NULL;
    if (event->type == YAML_SCALAR_EVENT) {
        if (event->data.scalar.anchor || event->data.scalar.tag ||
            strlen((const char *)event->data.scalar.value) != event->data.scalar.length) return NULL;
        const char *s = (const char *)event->data.scalar.value;
        if (event->data.scalar.style == YAML_PLAIN_SCALAR_STYLE) {
            if (!strcasecmp(s, "true") || !strcasecmp(s, "false")) return cJSON_CreateBool(!strcasecmp(s, "true"));
            if (!strcasecmp(s, "null") || !strcmp(s, "~") || !*s) return cJSON_CreateNull();
            if (!strcasecmp(s, ".inf") || !strcasecmp(s, "+.inf") || !strcasecmp(s, "-.inf") || !strcasecmp(s, ".nan")) return NULL;
            char *end;
            double number = strtod(s, &end);
            if (end != s && !*end && isfinite(number)) return cJSON_CreateNumber(number);
        }
        return cJSON_CreateString(s);
    }
    bool map = event->type == YAML_MAPPING_START_EVENT;
    if (!map && event->type != YAML_SEQUENCE_START_EVENT) return NULL;
    if (map ? (event->data.mapping_start.anchor || event->data.mapping_start.tag) :
              (event->data.sequence_start.anchor || event->data.sequence_start.tag)) return NULL;
    out = map ? cJSON_CreateObject() : cJSON_CreateArray();
    if (!out) return NULL;
    for (;;) {
        yaml_event_t child;
        if (!yaml_parser_parse(parser, &child)) goto fail;
        if (child.type == (map ? YAML_MAPPING_END_EVENT : YAML_SEQUENCE_END_EVENT)) {
            yaml_event_delete(&child);
            return out;
        }
        char *key = NULL;
        if (map) {
            if (child.type != YAML_SCALAR_EVENT || child.data.scalar.anchor || child.data.scalar.tag ||
                !child.data.scalar.length || strlen((const char *)child.data.scalar.value) != child.data.scalar.length ||
                field(out, (const char *)child.data.scalar.value)) { yaml_event_delete(&child); goto fail; }
            size_t n = child.data.scalar.length + 1;
            key = cJSON_malloc(n);
            if (key) memcpy(key, child.data.scalar.value, n);
            yaml_event_delete(&child);
            if (!key) goto fail;
            if (!yaml_parser_parse(parser, &child)) { cJSON_free(key); goto fail; }
        }
        cJSON *value = read_node(parser, &child, depth + 1);
        yaml_event_delete(&child);
        bool added = value && (map ? cJSON_AddItemToObject(out, key, value) : cJSON_AddItemToArray(out, value));
        cJSON_free(key);
        if (!added) { cJSON_Delete(value); goto fail; }
    }
fail:
    cJSON_Delete(out);
    return NULL;
}

static cJSON *read_yaml(const char *yaml, size_t size)
{
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) return NULL;
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, size);
    yaml_event_t event;
    cJSON *root = NULL;
    const yaml_event_type_t prefix[] = {YAML_STREAM_START_EVENT, YAML_DOCUMENT_START_EVENT};
    for (unsigned i = 0; i < 2; i++) {
        if (!yaml_parser_parse(&parser, &event)) goto done;
        bool valid = event.type == prefix[i];
        yaml_event_delete(&event);
        if (!valid) goto done;
    }
    if (!yaml_parser_parse(&parser, &event)) goto done;
    root = read_node(&parser, &event, 0);
    yaml_event_delete(&event);
    const yaml_event_type_t suffix[] = {YAML_DOCUMENT_END_EVENT, YAML_STREAM_END_EVENT};
    for (unsigned i = 0; root && i < 2; i++) {
        if (!yaml_parser_parse(&parser, &event)) { cJSON_Delete(root); root = NULL; break; }
        bool valid = event.type == suffix[i];
        yaml_event_delete(&event);
        if (!valid) { cJSON_Delete(root); root = NULL; }
    }
done:
    yaml_parser_delete(&parser);
    return root;
}

static bool token(const cJSON *o, const char *key, char *out)
{
    if (!curio_string(o, key, out, 129, NULL) || strlen(out) < 16) return false;
    for (const unsigned char *p = (const unsigned char *)out; *p; p++) if (*p < 33 || *p > 126) return false;
    return true;
}

static bool pair(const cJSON *o, const char *key, unsigned limit, unsigned *first, unsigned *second)
{
    const cJSON *values = field(o, key);
    if (!cJSON_IsArray(values) || cJSON_GetArraySize(values) != 2) return false;
    unsigned *out[] = {first, second};
    for (unsigned i = 0; i < 2; i++) {
        const cJSON *v = cJSON_GetArrayItem(values, (int)i);
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
            v->valuedouble > limit || floor(v->valuedouble) != v->valuedouble) return false;
        *out[i] = (unsigned)v->valuedouble;
    }
    return true;
}

static bool ntp_server_valid(const char *server)
{
    size_t n = strnlen(server, 128);
    if (!n || n == 128) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = server[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == ':' || c == '-')) return false;
    }
    return true;
}

static bool network_config_valid(const curio_network_config_t *network)
{
    if (!network || network->wifi_count > CURIO_WIFI_MAX_NETWORKS) return false;
    size_t hostname = strnlen(network->hostname, sizeof(network->hostname));
    if (!hostname || hostname > 32) return false;
    for (size_t i = 0; i < hostname; i++) {
        unsigned char c = network->hostname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || (c == '-' && i && i + 1 < hostname))) return false;
    }
    for (size_t i = 0; i < network->wifi_count; i++) {
        const curio_wifi_config_t *wifi = &network->wifi[i];
        size_t ssid = strnlen(wifi->ssid, sizeof(wifi->ssid));
        size_t password = strnlen(wifi->password, sizeof(wifi->password));
        if (!ssid || ssid > 32 || password == sizeof(wifi->password) ||
            (password && password < 8)) return false;
        if (password == 64) {
            for (size_t j = 0; j < password; j++) {
                char c = wifi->password[j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) return false;
            }
        }
        for (size_t j = 0; j < i; j++)
            if (!strcmp(wifi->ssid, network->wifi[j].ssid)) return false;
    }
    return true;
}

static bool tls_config_valid(const curio_tls_config_t *tls)
{
    size_t certificate = strnlen(tls->certificate, sizeof(tls->certificate));
    size_t private_key = strnlen(tls->private_key, sizeof(tls->private_key));
    return certificate < sizeof(tls->certificate) && private_key < sizeof(tls->private_key) &&
        (certificate == 0) == (private_key == 0);
}

static bool screen_parse(const cJSON *screen, curio_display_config_t *out)
{
    if (!fields(screen, "default_page|pages")) return false;
    const cJSON *pages = field(screen, "pages");
    if (!cJSON_IsArray(pages) || cJSON_GetArraySize(pages) < 1 || cJSON_GetArraySize(pages) > CURIO_LAYOUT_MAX_PAGES) return false;
    out->page_count = (size_t)cJSON_GetArraySize(pages);
    for (size_t i = 0; i < out->page_count; i++) {
        const cJSON *page = cJSON_GetArrayItem(pages, (int)i);
        curio_page_config_t *p = &out->pages[i];
        if (!fields(page, "id|title|widgets") || !curio_string(page, "id", p->id, sizeof(p->id), NULL) ||
            !curio_string(page, "title", p->title, sizeof(p->title), p->id)) return false;
        const cJSON *widgets = field(page, "widgets");
        if (!cJSON_IsArray(widgets) || cJSON_GetArraySize(widgets) > CURIO_LAYOUT_MAX_WIDGETS) return false;
        p->widget_count = (size_t)cJSON_GetArraySize(widgets);
        for (size_t j = 0; j < p->widget_count; j++) {
            const cJSON *widget = cJSON_GetArrayItem(widgets, (int)j);
            curio_widget_config_t *w = &p->widgets[j];
            char type[16], id[33];
            if (!curio_string(widget, "type", type, sizeof(type), NULL)) return false;
            snprintf(id, sizeof(id), "%s-%u", type, (unsigned)j + 1);
            w->type = curio_widget_type_find(type);
            if (!w->type || !curio_fields(widget, "id|type|title|at|size", w->type->fields) ||
                !curio_string(widget, "id", w->id, sizeof(w->id), id) ||
                !curio_string(widget, "title", w->title, sizeof(w->title), w->type->title) ||
                !pair(widget, "at", CURIO_LAYOUT_ROWS, &w->col, &w->row) ||
                !pair(widget, "size", CURIO_LAYOUT_ROWS, &w->cols, &w->rows)) return false;
            w->options = cJSON_malloc(w->type->options_size);
            if (!w->options || !w->type->parse(widget, w->options)) return false;

        }
    }
    return curio_string(screen, "default_page", out->default_page, sizeof(out->default_page), out->pages[0].id) && curio_layout_validate(out) == CURIO_OK;
}

static bool alerts_parse(const cJSON *alerts, curio_alert_config_t *out)
{
    curio_alert_config_default(out);
    if (alerts && !fields(alerts, "volume|vibration_strength|flash_enabled|rules")) return false;
    double value;
    if (!curio_number(alerts, "volume", 35, 0, 100, true, &value)) return false;
    out->volume = (uint8_t)value;
    if (!curio_number(alerts, "vibration_strength", 60, 0, 100, true, &value)) return false;
    out->vibration_strength = (uint8_t)value;
    if (!curio_boolean(alerts, "flash_enabled", false, &out->flash_enabled)) return false;
    const cJSON *rules = field(alerts, "rules");
    if (rules && (!cJSON_IsArray(rules) || cJSON_GetArraySize(rules) > CURIO_ALERT_MAX_RULES)) return false;
    out->rule_count = rules ? (size_t)cJSON_GetArraySize(rules) : 0;
    for (size_t i = 0; i < out->rule_count; i++) {
        const cJSON *rule = cJSON_GetArrayItem(rules, (int)i);
        curio_alert_rule_t *r = &out->rules[i];
        char id[49], type[24];
        snprintf(id, sizeof(id), "rule-%u", (unsigned)i + 1);
        if (!curio_string(rule, "type", type, sizeof(type), NULL) || !curio_string(rule, "id", r->id, sizeof(r->id), id)) return false;
        r->type = curio_alert_type_find(type);
        if (!r->type || !curio_fields(rule, "id|type|enabled|duration|sound|vibrate|flash", r->type->fields) ||
            !curio_boolean(rule, "enabled", true, &r->enabled) || !curio_boolean(rule, "sound", false, &r->sound) ||
            !curio_boolean(rule, "vibrate", false, &r->vibrate) || !curio_boolean(rule, "flash", false, &r->flash) ||
            !curio_number(rule, "duration", 10, 0, 300, true, &value)) return false;
        r->display_s = (uint16_t)value;
        r->options = cJSON_malloc(r->type->options_size);
        if (!r->options || !r->type->parse(rule, r->options)) return false;

    }
    return curio_alert_config_validate(out);
}

bool curio_config_parse(const char *yaml, size_t size, curio_config_t *config, char *error, size_t error_size)
{
    const char *reason = "invalid YAML configuration";
    if (error && error_size) *error = 0;
    if (!yaml || !size || size > CURIO_CONFIG_MAX_BYTES || !config) goto invalid;
    cJSON *root = read_yaml(yaml, size);
    curio_config_t *next = cJSON_malloc(sizeof(*next));
    bool ok = false;
    if (next) memset(next, 0, sizeof(*next));
    if (!root || !next) goto done;
    double value;
    const cJSON *access = field(root, "access"), *device = field(root, "device");
    const cJSON *network = field(root, "network"), *tls = field(root, "tls");
    reason = "invalid root fields or version";
    if (!fields(root, "version|access|device|screen|alerts|network|tls") || !curio_number(root, "version", 1, 1, 1, true, &value)) goto done;
    reason = "invalid access tokens or fields";
    if (!fields(access, "admin_token|collector_token") || !token(access, "admin_token", next->access.admin_token) ||
        !token(access, "collector_token", next->access.collector_token)) goto done;
    reason = "invalid device fields";
    if ((device && !fields(device, "brightness|rotation|auto_rotate|ntp_server")) || !curio_number(device, "brightness", 75, 0, 100, true, &value)) goto done;
    next->device.brightness = (uint8_t)value;
    if (!curio_number(device, "rotation", 0, 0, 270, true, &value) || (unsigned)value % 90) goto done;
    next->device.rotation = (uint16_t)value;
    if (!curio_boolean(device, "auto_rotate", false, &next->device.auto_rotate)) goto done;
    if (!curio_string(device, "ntp_server", next->device.ntp_server, sizeof(next->device.ntp_server), "pool.ntp.org") ||
        !ntp_server_valid(next->device.ntp_server)) goto done;
    reason = "invalid network fields";
    if ((network && !fields(network, "wifi|hostname")) ||
        !curio_string(network, "hostname", next->network.hostname, sizeof(next->network.hostname), "curio")) goto done;
    const cJSON *wifi = field(network, "wifi");
    if (wifi && (!cJSON_IsArray(wifi) || cJSON_GetArraySize(wifi) > CURIO_WIFI_MAX_NETWORKS)) goto done;
    next->network.wifi_count = wifi ? (size_t)cJSON_GetArraySize(wifi) : 0;
    for (size_t i = 0; i < next->network.wifi_count; i++) {
        const cJSON *entry = cJSON_GetArrayItem(wifi, (int)i);
        curio_wifi_config_t *out = &next->network.wifi[i];
        if (!fields(entry, "ssid|password") ||
            !curio_string(entry, "ssid", out->ssid, sizeof(out->ssid), NULL) ||
            !curio_string(entry, "password", out->password, sizeof(out->password), "")) goto done;
    }
    if (!network_config_valid(&next->network)) goto done;
    reason = "invalid TLS fields";
    if (tls && (!fields(tls, "certificate|private_key") ||
        !curio_string(tls, "certificate", next->tls.certificate, sizeof(next->tls.certificate), NULL) ||
        !curio_string(tls, "private_key", next->tls.private_key, sizeof(next->tls.private_key), NULL) ||
        !tls_config_valid(&next->tls) || !*next->tls.certificate)) goto done;
    reason = "invalid screen pages or widgets";
    if (!screen_parse(field(root, "screen"), &next->display)) goto done;
    reason = "invalid alerts rules or volume";
    if (!alerts_parse(field(root, "alerts"), &next->alerts)) goto done;
    curio_config_free(config);
    *config = *next;
    memset(next, 0, sizeof(*next));
    ok = true;
done:
    cJSON_Delete(root);
    if (next) curio_config_free(next);
    cJSON_free(next);
    if (ok) return true;
invalid:
    if (error && error_size) snprintf(error, error_size, "%s", reason);
    return false;
}

static cJSON *object(cJSON *parent, const char *key) { return cJSON_AddObjectToObject(parent, key); }

static bool put_pair(cJSON *o, const char *key, unsigned first, unsigned second)
{
    double values[] = {first, second};
    cJSON *array = cJSON_CreateDoubleArray(values, 2);
    if (array && cJSON_AddItemToObject(o, key, array)) return true;
    cJSON_Delete(array);
    return false;
}

static cJSON *config_tree(const curio_config_t *c)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    if (!curio_put_number(root, "version", 1)) goto fail;
    cJSON *access = object(root, "access"), *device = object(root, "device"), *screen = object(root, "screen"), *alerts = object(root, "alerts");
    if (!access || !device || !screen || !alerts ||
        !curio_put_string(access, "admin_token", c->access.admin_token) || !curio_put_string(access, "collector_token", c->access.collector_token) ||
        !curio_put_number(device, "brightness", c->device.brightness) || !curio_put_number(device, "rotation", c->device.rotation) ||
        !curio_put_bool(device, "auto_rotate", c->device.auto_rotate) || !curio_put_number(alerts, "volume", c->alerts.volume) ||
        !curio_put_number(alerts, "vibration_strength", c->alerts.vibration_strength) ||
        !curio_put_bool(alerts, "flash_enabled", c->alerts.flash_enabled)) goto fail;
    if (strcmp(c->device.ntp_server, "pool.ntp.org") && !curio_put_string(device, "ntp_server", c->device.ntp_server)) goto fail;
    if (c->network.wifi_count || strcmp(c->network.hostname, "curio")) {
        cJSON *network = object(root, "network");
        if (!network || (strcmp(c->network.hostname, "curio") &&
            !curio_put_string(network, "hostname", c->network.hostname))) goto fail;
        if (c->network.wifi_count) {
            cJSON *wifi = cJSON_AddArrayToObject(network, "wifi");
            if (!wifi) goto fail;
            for (size_t i = 0; i < c->network.wifi_count; i++) {
                cJSON *entry = cJSON_CreateObject();
                if (!entry || !cJSON_AddItemToArray(wifi, entry)) { cJSON_Delete(entry); goto fail; }
                if (!curio_put_string(entry, "ssid", c->network.wifi[i].ssid) ||
                    (*c->network.wifi[i].password &&
                     !curio_put_string(entry, "password", c->network.wifi[i].password))) goto fail;
            }
        }
    }
    if (*c->tls.certificate) {
        cJSON *tls = object(root, "tls");
        if (!tls || !curio_put_string(tls, "certificate", c->tls.certificate) ||
            !curio_put_string(tls, "private_key", c->tls.private_key)) goto fail;
    }
    if (strcmp(c->display.default_page, c->display.pages[0].id) && !curio_put_string(screen, "default_page", c->display.default_page)) goto fail;
    cJSON *pages = cJSON_AddArrayToObject(screen, "pages");
    cJSON *rules = c->alerts.rule_count ? cJSON_AddArrayToObject(alerts, "rules") : NULL;
    if (!pages || (c->alerts.rule_count && !rules)) goto fail;
    for (size_t i = 0; i < c->display.page_count; i++) {
        const curio_page_config_t *p = &c->display.pages[i];
        cJSON *page = cJSON_CreateObject();
        if (!page || !cJSON_AddItemToArray(pages, page)) { cJSON_Delete(page); goto fail; }
        if (!curio_put_string(page, "id", p->id) || (strcmp(p->title, p->id) && !curio_put_string(page, "title", p->title))) goto fail;
        cJSON *widgets = cJSON_AddArrayToObject(page, "widgets");
        if (!widgets) goto fail;
        for (size_t j = 0; j < p->widget_count; j++) {
            const curio_widget_config_t *w = &p->widgets[j];
            cJSON *widget = cJSON_CreateObject();
            if (!widget || !cJSON_AddItemToArray(widgets, widget)) { cJSON_Delete(widget); goto fail; }
            if (!curio_put_string(widget, "id", w->id) || !curio_put_string(widget, "type", w->type->id) ||
                (strcmp(w->title, w->type->title) && !curio_put_string(widget, "title", w->title)) ||
                !put_pair(widget, "at", w->col, w->row) || !put_pair(widget, "size", w->cols, w->rows) ||
                !w->type->write(widget, w->options)) goto fail;

        }
    }
    for (size_t i = 0; i < c->alerts.rule_count; i++) {
        const curio_alert_rule_t *r = &c->alerts.rules[i];
        cJSON *rule = cJSON_CreateObject();
        if (!rule || !cJSON_AddItemToArray(rules, rule)) { cJSON_Delete(rule); goto fail; }
        if (!curio_put_string(rule, "id", r->id) || !curio_put_string(rule, "type", r->type->id) ||
            (!r->enabled && !curio_put_bool(rule, "enabled", false)) ||
            (r->display_s != 10 && !curio_put_number(rule, "duration", r->display_s)) ||
            (r->sound && !curio_put_bool(rule, "sound", true)) ||
            (r->vibrate && !curio_put_bool(rule, "vibrate", true)) ||
            (r->flash && !curio_put_bool(rule, "flash", true)) || !r->type->write(rule, r->options)) goto fail;

    }
    return root;
fail:
    cJSON_Delete(root);
    return NULL;
}

typedef struct { char *data; size_t size; } yaml_output_t;
static int write_yaml(void *data, unsigned char *buffer, size_t size)
{
    yaml_output_t *out = data;
    if (size > CURIO_CONFIG_MAX_BYTES - out->size) return 0;
    memcpy(out->data + out->size, buffer, size);
    out->size += size;
    out->data[out->size] = 0;
    return 1;
}
static bool emit_scalar(yaml_emitter_t *emitter, const char *s, bool string_value)
{
    yaml_event_t event;
    yaml_scalar_style_t style = YAML_PLAIN_SCALAR_STYLE;
    if (string_value) {
        char *end;
        (void)strtod(s, &end);
        if (strlen(s) == 6 && (s[0] == '+' || s[0] == '-') && s[3] == ':')
            style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
        if (!*s || !strcasecmp(s, "true") || !strcasecmp(s, "false") || !strcasecmp(s, "null") || !strcmp(s, "~") ||
            !strcasecmp(s, ".inf") || !strcasecmp(s, "+.inf") || !strcasecmp(s, "-.inf") || !strcasecmp(s, ".nan") ||
            (end != s && !*end)) style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    }
    return yaml_scalar_event_initialize(&event, NULL, NULL, (const unsigned char *)s, (int)strlen(s), 1, 1, style) && yaml_emitter_emit(emitter, &event);
}
static bool emit_node(yaml_emitter_t *emitter, const cJSON *node)
{
    yaml_event_t event;
    if (cJSON_IsObject(node) || cJSON_IsArray(node)) {
        bool map = cJSON_IsObject(node);
        bool initialized = map ? yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE) :
            yaml_sequence_start_event_initialize(&event, NULL, NULL, 1,
                node->string && (!strcmp(node->string, "at") || !strcmp(node->string, "size")) ?
                YAML_FLOW_SEQUENCE_STYLE : YAML_BLOCK_SEQUENCE_STYLE);
        if (!initialized || !yaml_emitter_emit(emitter, &event)) return false;
        for (const cJSON *v = node->child; v; v = v->next) {
            if (map && !emit_scalar(emitter, v->string, true)) return false;
            if (!emit_node(emitter, v)) return false;
        }
        initialized = map ? yaml_mapping_end_event_initialize(&event) : yaml_sequence_end_event_initialize(&event);
        return initialized && yaml_emitter_emit(emitter, &event);
    }
    if (cJSON_IsString(node)) {
        if (node->string && (!strcmp(node->string, "certificate") || !strcmp(node->string, "private_key"))) {
            yaml_event_t scalar;
            return yaml_scalar_event_initialize(&scalar, NULL, NULL, (const unsigned char *)node->valuestring,
                (int)strlen(node->valuestring), 1, 1, YAML_LITERAL_SCALAR_STYLE) && yaml_emitter_emit(emitter, &scalar);
        }
        return emit_scalar(emitter, node->valuestring, true);
    }
    if (cJSON_IsBool(node)) return emit_scalar(emitter, cJSON_IsTrue(node) ? "true" : "false", false);
    if (cJSON_IsNumber(node)) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.17g", node->valuedouble);
        return emit_scalar(emitter, buffer, false);
    }
    return false;
}
char *curio_config_yaml(const curio_config_t *config)
{
    if (!config || curio_layout_validate(&config->display) != CURIO_OK || config->device.brightness > 100 ||
        config->device.rotation > 270 || config->device.rotation % 90) return NULL;
    if (!ntp_server_valid(config->device.ntp_server)) return NULL;
    if (!network_config_valid(&config->network) || !tls_config_valid(&config->tls)) return NULL;
    if (!curio_alert_config_validate(&config->alerts)) return NULL;
    cJSON *tree = config_tree(config);
    yaml_output_t out = {.data = cJSON_malloc(CURIO_CONFIG_MAX_BYTES + 1)};
    yaml_emitter_t emitter;
    bool initialized = yaml_emitter_initialize(&emitter);
    bool ok = false;
    if (!tree || !out.data || !initialized) goto done;
    yaml_emitter_set_output(&emitter, write_yaml, &out);
    yaml_emitter_set_unicode(&emitter, 1);
    yaml_emitter_set_indent(&emitter, 2);
    yaml_event_t event;
    if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) || !yaml_emitter_emit(&emitter, &event) ||
        !yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1) || !yaml_emitter_emit(&emitter, &event) ||
        !emit_node(&emitter, tree) || !yaml_document_end_event_initialize(&event, 1) || !yaml_emitter_emit(&emitter, &event) ||
        !yaml_stream_end_event_initialize(&event) || !yaml_emitter_emit(&emitter, &event)) goto done;
    curio_config_t *verified = cJSON_malloc(sizeof(*verified));
    if (verified) {
        memset(verified, 0, sizeof(*verified));
        ok = curio_config_parse(out.data, out.size, verified, NULL, 0);
        curio_config_free(verified);
        cJSON_free(verified);
    }
done:
    if (initialized) yaml_emitter_delete(&emitter);
    cJSON_Delete(tree);
    if (!ok) { cJSON_free(out.data); return NULL; }
    return out.data;
}

void curio_config_free(curio_config_t *config)
{
    curio_layout_free(&config->display);
    curio_alert_config_free(&config->alerts);
    memset(config, 0, sizeof(*config));
}
bool curio_config_clone(curio_config_t *dst, const curio_config_t *src)
{
    *dst = *src;
    memset(&dst->display, 0, sizeof(dst->display));
    memset(&dst->alerts, 0, sizeof(dst->alerts));
    if (curio_layout_clone(&dst->display, &src->display) && curio_alert_config_clone(&dst->alerts, &src->alerts)) return true;
    curio_config_free(dst);
    return false;
}
bool curio_config_equal(const curio_config_t *a, const curio_config_t *b)
{
    return !memcmp(&a->device, &b->device, sizeof(a->device)) &&
        !memcmp(&a->access, &b->access, sizeof(a->access)) &&
        !memcmp(&a->network, &b->network, sizeof(a->network)) &&
        !memcmp(&a->tls, &b->tls, sizeof(a->tls)) &&
        curio_layout_equal(&a->display, &b->display) && curio_alert_config_equal(&a->alerts, &b->alerts);
}
