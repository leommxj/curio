#include "config.h"
#include "curio_module.h"
#include <string.h>

static bool validate(const void *options)
{
    const curio_agents_config_t *c = options;
    if (!curio_identifier(c->source_id, 48) || c->host_count > CURIO_MAX_HOSTS || (c->all_hosts && c->host_count) ||
        c->filter > CURIO_FILTER_WORKING || strlen(c->project) > 48 || strlen(c->agent) > 96) return false;
    for (size_t i = 0; i < c->host_count; i++) {
        if (!curio_identifier(c->hosts[i], 48)) return false;
        for (size_t j = 0; j < i; j++) if (!strcmp(c->hosts[i], c->hosts[j])) return false;
    }
    return true;
}
static bool parse(const cJSON *o, void *options)
{
    curio_agents_config_t *c = options;
    memset(c, 0, sizeof(*c));
    char filter[16];
    if (!curio_string(o, "source", c->source_id, sizeof(c->source_id), "herdr") ||
        !curio_string(o, "filter", filter, sizeof(filter), "all") ||
        !curio_string(o, "project", c->project, sizeof(c->project), "") ||
        !curio_string(o, "agent", c->agent, sizeof(c->agent), "") ||
        !curio_boolean(o, "auto_page", true, &c->auto_page)) return false;
    if (!strcmp(filter, "all")) c->filter = CURIO_FILTER_ALL;
    else if (!strcmp(filter, "attention")) c->filter = CURIO_FILTER_ATTENTION;
    else if (!strcmp(filter, "working")) c->filter = CURIO_FILTER_WORKING;
    else return false;
    const cJSON *hosts = cJSON_GetObjectItemCaseSensitive(o, "hosts");
    if (!hosts) c->all_hosts = true;
    else {
        if (!cJSON_IsArray(hosts) || cJSON_GetArraySize(hosts) > CURIO_MAX_HOSTS) return false;
        c->host_count = (size_t)cJSON_GetArraySize(hosts);
        for (size_t i = 0; i < c->host_count; i++) {
            const cJSON *h = cJSON_GetArrayItem(hosts, (int)i);
            if (!cJSON_IsString(h) || strlen(h->valuestring) >= sizeof(c->hosts[i])) return false;
            strcpy(c->hosts[i], h->valuestring);
        }
        if (c->host_count == 1 && !strcmp(c->hosts[0], "*")) { c->all_hosts = true; c->host_count = 0; c->hosts[0][0] = 0; }
    }
    return validate(c);
}
static bool write(cJSON *o, const void *options)
{
    const curio_agents_config_t *c = options;
    if (!c->all_hosts) {
        cJSON *hosts = cJSON_AddArrayToObject(o, "hosts");
        if (!hosts) return false;
        for (size_t i = 0; i < c->host_count; i++) {
            cJSON *h = cJSON_CreateString(c->hosts[i]);
            if (!h || !cJSON_AddItemToArray(hosts, h)) { cJSON_Delete(h); return false; }
        }
    }
    return (!strcmp(c->source_id, "herdr") || curio_put_string(o, "source", c->source_id)) &&
        (c->filter == CURIO_FILTER_ALL || curio_put_string(o, "filter", c->filter == CURIO_FILTER_ATTENTION ? "attention" : "working")) &&
        (c->auto_page || curio_put_bool(o, "auto_page", false)) &&
        (!*c->project || curio_put_string(o, "project", c->project)) && (!*c->agent || curio_put_string(o, "agent", c->agent));
}
static bool paging(const void *options) { return ((const curio_agents_config_t *)options)->auto_page; }
static void set_paging(void *options, bool enabled) { ((curio_agents_config_t *)options)->auto_page = enabled; }
const curio_widget_type_t curio_widget_agents = {
    .id = "agents", .title = "Agents", .fields = "hosts|source|filter|auto_page|project|agent",
    .min_cols = 3, .min_rows = 3, .options_size = sizeof(curio_agents_config_t),
    .parse = parse, .validate = validate, .write = write, .paging = paging, .set_paging = set_paging,
};
