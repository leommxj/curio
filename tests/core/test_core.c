#include "cJSON.h"
#include "curio_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static curio_core_t core, before;
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static const char *agent =
    "{\"id\":\"same\",\"name\":\"Codex\",\"project\":\"curio\",\"state\":\"working\"}";

static char *snapshot(const char *host, bool source_ok, const char *agents)
{
    const char *format = "{\"v\":2,\"host_id\":\"%s\",\"host_name\":\"工作站\",\"sources\":{"
                         "\"herdr\":{\"kind\":\"agents\",\"status\":\"%s\",\"sample_age_ms\":0,\"agents\":[%s]}}}";
    size_t capacity = strlen(format) + strlen(host) + strlen(agents) + 10;
    char *body = malloc(capacity);
    CHECK(body != NULL);
    snprintf(body, capacity, format, host, source_ok ? "ok" : "error", agents);
    return body;
}

static curio_result_t apply(const char *host, bool source_ok, const char *agents, uint64_t now)
{
    char *body = snapshot(host, source_ok, agents);
    curio_result_t result = curio_core_apply_json(&core, body, strlen(body), now);
    free(body);
    return result;
}

static void reject_atomic(const char *body, size_t length)
{
    before = core;
    CHECK(curio_core_apply_json(&core, body, length, 900000) == CURIO_INVALID);
    CHECK(memcmp(&before, &core, sizeof(core)) == 0);
}

static const curio_source_t *herdr(const curio_host_t *host) { return curio_host_source(host, "herdr"); }
static curio_health_t health(const curio_host_t *host, uint64_t now) { return curio_host_health(host, now) == CURIO_HEALTH_OFFLINE ? CURIO_HEALTH_OFFLINE : curio_source_health(herdr(host), now); }
static uint64_t success_at(const curio_host_t *host) { return herdr(host)->received_ms - herdr(host)->sample_age_ms; }
static cJSON *agent_array(cJSON *root) { return cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "sources"), "herdr"), "agents"); }

static void test_lifecycle(void)
{
    if (core.allocator.free)
        curio_core_destroy(&core);
    curio_core_init(&core);
    CHECK(apply("host", true, agent, 0) == CURIO_OK);
    curio_host_t *host = &core.hosts[0];
    CHECK(herdr(host)->has_success && success_at(host) == 0);
    CHECK(health(host, 10000) == CURIO_HEALTH_FRESH);
    CHECK(health(host, 10001) == CURIO_HEALTH_STALE);
    CHECK(health(host, 30000) == CURIO_HEALTH_STALE);
    CHECK(health(host, 30001) == CURIO_HEALTH_OFFLINE);
    CHECK(apply("host", false, "", 9000) == CURIO_OK);
    CHECK(herdr(host)->agent_count == 1 && success_at(host) == 0);
    CHECK(health(host, 9000) == CURIO_HEALTH_UNAVAILABLE);
    CHECK(apply("host", false, "", 38000) == CURIO_OK);
    CHECK(success_at(host) == 0);
    CHECK(health(host, 38000) == CURIO_HEALTH_UNAVAILABLE);
    CHECK(health(host, 68001) == CURIO_HEALTH_OFFLINE);
    CHECK(apply("host", true, agent, 70000) == CURIO_OK);
    CHECK(health(host, 70000) == CURIO_HEALTH_FRESH);
    CHECK(apply("host", true, "", 70001) == CURIO_OK);
    CHECK(herdr(host)->agent_count == 0 && herdr(host)->has_success);
    CHECK(apply("failed-first", false, "", 70002) == CURIO_OK);
    CHECK(!herdr(&core.hosts[1])->has_success && herdr(&core.hosts[1])->agent_count == 0);
    CHECK(health(&core.hosts[1], 70002) == CURIO_HEALTH_UNAVAILABLE);
    CHECK(health(&core.hosts[2], 0) == CURIO_HEALTH_OFFLINE);
    CHECK(health(host, 1) == CURIO_HEALTH_FRESH);
}

static void test_atomic_validation(void)
{
    if (core.allocator.free)
        curio_core_destroy(&core);
    curio_core_init(&core);
    CHECK(apply("host", true, agent, 123) == CURIO_OK);
    const char *invalid[] = {
        "",
        "null",
        "[]",
        "{}",
        "{",
        "{}{}",
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"x\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}},\"v\":2}",
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"x\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[null]}}}",
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"x\\u0000suffix\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}",
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"\\ud800\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}",
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"\\udc00\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}",
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"x\nraw\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}",
        "{\"v\":1,\"host_id\":\"host\",\"host_name\":\"x\",\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}",
                             "\x01{}",
                             "{\"unknown\":\"\xc0\xaf\"}",
                             "{\"unknown\":\"\xf4\x90\x80\x80\"}",
                             "{\"unknown\":\"\\uZZZZ\"}",
                             "{\"unknown\":\"\\u000X\"}",
                             "{\"unknown\":\"\\q\"}",
                             "{\"unknown\":\"\\ud800\\u0000\"}",
                             "{\"unknown\":\"\\udc00\"}"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        reject_atomic(invalid[i], strlen(invalid[i]));
    const char *bad_hosts[] = {"", "a/b", "a b", "主机",
                               "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvw"};
    for (size_t i = 0; i < sizeof(bad_hosts) / sizeof(bad_hosts[0]); ++i) {
        char *body = snapshot(bad_hosts[i], true, agent);
        reject_atomic(body, strlen(body));
        free(body);
    }
    char *body = snapshot("host", false, agent);
    reject_atomic(body, strlen(body));
    free(body);
    size_t len = strlen(agent) * 2 + 2;
    char *duplicates = malloc(len);
    CHECK(duplicates != NULL);
    snprintf(duplicates, len, "%s,%s", agent, agent);
    body = snapshot("host", true, duplicates);
    reject_atomic(body, strlen(body));
    free(duplicates);
    free(body);
    body = snapshot("host", true, agent);
    const char *fields[] = {"v", "sources", "host_name", "host_id"};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        cJSON *root = cJSON_Parse(body);
        CHECK(root != NULL);
        cJSON_DeleteItemFromObjectCaseSensitive(root, fields[i]);
        char *changed = cJSON_PrintUnformatted(root);
        reject_atomic(changed, strlen(changed));
        cJSON_free(changed);
        cJSON_Delete(root);
    }
    const char *source_fields[] = {"kind", "status", "sample_age_ms", "agents"};
    for (size_t i = 0; i < sizeof(source_fields) / sizeof(*source_fields); i++) {
        cJSON *candidate = cJSON_Parse(body);
        cJSON *source = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(candidate, "sources"), "herdr");
        cJSON_DeleteItemFromObjectCaseSensitive(source, source_fields[i]);
        char *changed = cJSON_PrintUnformatted(candidate);
        reject_atomic(changed, strlen(changed));
        cJSON_free(changed);
        cJSON_Delete(candidate);
    }
    cJSON *root = cJSON_Parse(body);
    cJSON_ReplaceItemInObjectCaseSensitive(root, "v", cJSON_CreateNumber(1.5));
    char *changed = cJSON_PrintUnformatted(root);
    reject_atomic(changed, strlen(changed));
    cJSON_free(changed);
    cJSON_Delete(root);
    char *nul_body = malloc(strlen(body) + 1);
    memcpy(nul_body, body, strlen(body) + 1);
    nul_body[10] = 0;
    reject_atomic(nul_body, strlen(body));
    free(nul_body);
    root = cJSON_Parse(body);
    cJSON_AddStringToObject(root, "future", "ignored");
    cJSON *entry = cJSON_GetArrayItem(agent_array(root), 0);
    cJSON_AddNumberToObject(entry, "future", 42);
    changed = cJSON_PrintUnformatted(root);
    CHECK(curio_core_apply_json(&core, changed, strlen(changed), 124) == CURIO_OK);
    cJSON_free(changed);
    cJSON_Delete(root);
    free(body);
    const char *bad_numbers[] = {"01", "1.", "1e", "-", "+1", "1e+"};
    for (size_t i = 0; i < sizeof(bad_numbers) / sizeof(bad_numbers[0]); ++i) {
        char number_body[200];
        snprintf(number_body, sizeof(number_body),
                 "{\"v\":%s,\"host_id\":\"host\",\"host_name\":\"h\",\"sources\":{\"herdr\":{"
                 "\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}",
                 bad_numbers[i]);
        reject_atomic(number_body, strlen(number_body));
    }
    char too_large[CURIO_MAX_BODY_BYTES + 1];
    memset(too_large, ' ', sizeof(too_large));
    before = core;
    CHECK(curio_core_apply_json(&core, too_large, sizeof(too_large), 0) == CURIO_TOO_LARGE);
    CHECK(memcmp(&before, &core, sizeof(core)) == 0);
    CHECK(curio_core_apply_json(&core, NULL, 1, 0) == CURIO_INVALID);
    const char *unicode = "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"\\ud83d\\ude00\","
                          "\"sources\":{\"herdr\":{\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[]}}}";
    CHECK(curio_core_apply_json(&core, unicode, strlen(unicode), 125) == CURIO_OK);
    CHECK(strcmp(core.hosts[0].host_name, "😀") == 0);
    body = snapshot("host", true, agent);
    memcpy(too_large, body, strlen(body));
    memset(too_large + strlen(body), ' ', CURIO_MAX_BODY_BYTES - strlen(body));
    CHECK(curio_core_apply_json(&core, too_large, CURIO_MAX_BODY_BYTES, 125) == CURIO_OK);
    free(body);
    body = snapshot("host", true, "");
    size_t base_len = strlen(body);
    char deep[1000];
    memcpy(deep, body, base_len - 1);
    size_t pos = base_len - 1;
    memcpy(deep + pos, ",\"future\":", 10);
    pos += 10;
    for (unsigned i = 0; i < 32; ++i)
        deep[pos++] = '[';
    deep[pos++] = '0';
    for (unsigned i = 0; i < 32; ++i)
        deep[pos++] = ']';
    deep[pos++] = '}';
    reject_atomic(deep, pos);
    free(body);
}

static void test_limits(void)
{
    if (core.allocator.free)
        curio_core_destroy(&core);
    curio_core_init(&core);
    char *body = snapshot("h", true, agent);
    cJSON *root = cJSON_Parse(body);
    free(body);
    const char *names[] = {"id", "name", "project", "host_name", "host_id"};
    const size_t sizes[] = {96, 64, 48, 48, 48};
    cJSON *entry = cJSON_GetArrayItem(agent_array(root), 0);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        char text[100];
        memset(text, 'a', sizes[i] + 1);
        text[sizes[i]] = 0;
        cJSON *object = i < 3 ? entry : root;
        cJSON_ReplaceItemInObjectCaseSensitive(object, names[i], cJSON_CreateString(text));
        body = cJSON_PrintUnformatted(root);
        CHECK(curio_core_apply_json(&core, body, strlen(body), 0) == CURIO_OK);
        cJSON_free(body);
        text[sizes[i]] = 'a';
        text[sizes[i] + 1] = 0;
        cJSON_ReplaceItemInObjectCaseSensitive(object, names[i], cJSON_CreateString(text));
        body = cJSON_PrintUnformatted(root);
        reject_atomic(body, strlen(body));
        cJSON_free(body);
        text[sizes[i]] = 0;
        cJSON_ReplaceItemInObjectCaseSensitive(object, names[i], cJSON_CreateString(text));
    }
    cJSON_ReplaceItemInObjectCaseSensitive(entry, "name", cJSON_CreateString("你好😀"));
    body = cJSON_PrintUnformatted(root);
    CHECK(curio_core_apply_json(&core, body, strlen(body), 1) == CURIO_OK);
    cJSON_free(body);
    cJSON_Delete(root);
    root = cJSON_CreateArray();
    for (unsigned i = 0; i < 17; ++i) {
        entry = cJSON_Parse(agent);
        char id[10];
        snprintf(id, sizeof(id), "%u", i);
        cJSON_ReplaceItemInObjectCaseSensitive(entry, "id", cJSON_CreateString(id));
        cJSON_AddItemToArray(root, entry);
        if (i >= 15) {
            char *list = cJSON_PrintUnformatted(root);
            list[strlen(list) - 1] = 0;
            body = snapshot("capacity", true, list + 1);
            if (i == 15)
                CHECK(curio_core_apply_json(&core, body, strlen(body), 2) == CURIO_OK);
            else
                reject_atomic(body, strlen(body));
            cJSON_free(list);
            free(body);
        }
    }
    cJSON_Delete(root);
}

static void test_hosts_and_eviction(void)
{
    if (core.allocator.free)
        curio_core_destroy(&core);
    curio_core_init(&core);
    CHECK(apply("a", true, agent, 0) == CURIO_OK);
    CHECK(apply("b", true, agent, 1) == CURIO_OK);
    CHECK(apply("c", true, agent, 2) == CURIO_OK);
    CHECK(apply("d", true, agent, 3) == CURIO_OK);
    CHECK(herdr(&core.hosts[0])->agent_count == 1 && herdr(&core.hosts[1])->agent_count == 1);
    before = core;
    CHECK(apply("e", true, agent, 330000) == CURIO_CAPACITY);
    CHECK(memcmp(&before, &core, sizeof(core)) == 0);
    CHECK(apply("a", false, "", 330000) == CURIO_OK);
    CHECK(apply("e", true, agent, 330001) == CURIO_CAPACITY);
    CHECK(apply("e", false, "", 330002) == CURIO_OK);
    CHECK(strcmp(core.hosts[1].host_id, "e") == 0);
    CHECK(!herdr(&core.hosts[1])->has_success && herdr(&core.hosts[1])->agent_count == 0);
    CHECK(strcmp(core.hosts[0].host_id, "a") == 0 && herdr(&core.hosts[0])->agent_count == 1);
    CHECK(apply("e", true, "", 330003) == CURIO_OK);
    CHECK(herdr(&core.hosts[1])->has_success && herdr(&core.hosts[1])->agent_count == 0);
}

static void test_state_and_catalog(void)
{
    curio_core_destroy(&core);
    curio_core_init(&core);
    CHECK(apply("a", true, agent, 0) == CURIO_OK);
    CHECK(apply("b", true, agent, 1) == CURIO_OK);
    char *json = curio_core_state_v2_json(&core, 10001);
    CHECK(json != NULL);
    cJSON *root = cJSON_Parse(json);
    CHECK(root && cJSON_GetObjectItemCaseSensitive(root, "v")->valueint == 2);
    cJSON *hosts = cJSON_GetObjectItemCaseSensitive(root, "hosts");
    CHECK(cJSON_GetArraySize(hosts) == 2);
    cJSON *first = cJSON_GetArrayItem(hosts, 0), *second = cJSON_GetArrayItem(hosts, 1);
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(first, "health")->valuestring, "stale"));
    CHECK(!strcmp(cJSON_GetObjectItemCaseSensitive(second, "health")->valuestring, "fresh"));
    CHECK(cJSON_GetArraySize(agent_array(first)) == 1);
    cJSON_Delete(root);
    curio_core_free_json(json);
    json = curio_core_catalog_json(&core, 10001);
    CHECK(json && strstr(json, "herdr") && !strstr(json, "\"agents\":["));
    curio_core_free_json(json);
}

static void test_v2(void)
{
    curio_core_destroy(&core);
    curio_core_init(&core);
    const char *ok =
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"Host\",\"sources\":{\"system.cpu\":{"
        "\"kind\":\"metrics\",\"status\":\"ok\",\"sample_age_ms\":500,\"metrics\":{\"usage_pct\":{"
        "\"unit\":\"percent\",\"value\":42},\"logical_count\":{\"unit\":\"count\",\"value\":8}}}}}";
    CHECK(curio_core_apply_json(&core, ok, strlen(ok), 1000) == CURIO_OK);
    const curio_host_t *h = curio_core_host(&core, "host");
    const curio_source_t *src = curio_host_source(h, "system.cpu");
    CHECK(src && src->metric_count == 2);
    CHECK(curio_metric_age(curio_source_metric(src, "usage_pct"), 2000) == 1500);
    CHECK(curio_source_health(src, 10501) == CURIO_HEALTH_STALE);
    CHECK(apply("host", true, agent, 2000) == CURIO_OK);
    CHECK(curio_host_source(h, "system.cpu") == NULL);
    CHECK(herdr(h)->agent_count == 1);
    CHECK(curio_core_apply_json(&core, ok, strlen(ok), 2000) == CURIO_OK);
    const char *fail = "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"Host\",\"sources\":{\"system."
                       "cpu\":{\"kind\":\"metrics\",\"status\":\"error\",\"sample_age_ms\":0,"
                       "\"metrics\":{\"usage_pct\":{\"unit\":\"percent\"}}}}}";
    CHECK(curio_core_apply_json(&core, fail, strlen(fail), 3000) == CURIO_OK);
    src = curio_host_source(h, "system.cpu");
    const curio_metric_t *m = curio_source_metric(src, "usage_pct");
    CHECK(m->has_value && m->value == 42);
    CHECK(curio_metric_age(m, 4000) == 2500);
    CHECK(src->metric_count == 1);
    CHECK(herdr(h) == NULL);
    CHECK(curio_source_health(src, 4000) == CURIO_HEALTH_UNAVAILABLE);
    char *json = curio_core_state_v2_json(&core, 4000);
    CHECK(json && strstr(json, "system.cpu"));
    curio_core_free_json(json);
    const char *empty = "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"Host\",\"sources\":{}}";
    CHECK(curio_core_apply_json(&core, empty, strlen(empty), 5000) == CURIO_OK);
    CHECK(h->source_count == 0);
    curio_snapshot_t *snapshot = NULL;
    CHECK(curio_core_parse(&core, ok, strlen(ok), &snapshot) == CURIO_OK);
    CHECK(h->source_count == 0);
    CHECK(curio_core_apply(&core, snapshot, 6000) == CURIO_OK);
    curio_core_snapshot_free(&core, snapshot);
    char bad[1024];
    strcpy(bad, ok);
    char *value = strstr(bad, "42");
    value[0] = '9';
    value[1] = '9'; /* still valid */
    CHECK(curio_core_apply_json(&core, bad, strlen(bad), 7000) == CURIO_OK);
}

static size_t oom_calls, oom_fail;
static void *oom_alloc(size_t n)
{
    if (oom_calls++ == oom_fail)
        return NULL;
    return malloc(n);
}
static void test_v2_oom(void)
{
    cJSON_Hooks hooks = {oom_alloc, free};
    cJSON_InitHooks(&hooks);
    for (oom_fail = 0; oom_fail < 150; ++oom_fail) {
        oom_calls = 0;
        char *json = curio_core_state_v2_json(&core, 8000);
        if (oom_calls > oom_fail)
            CHECK(json == NULL);
        curio_core_free_json(json);
    }
    cJSON_InitHooks(NULL);
}

static void test_v2_limits(void)
{
    curio_core_destroy(&core);
    curio_core_init(&core);
    char body[8192];
    size_t n = (size_t)snprintf(
        body, sizeof(body),
        "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"cpu\":{\"kind\":\"metrics\","
        "\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{");
    for (int i = 0; i < 33; i++)
        n += (size_t)snprintf(body + n, sizeof(body) - n,
                              "%s\"m%d\":{\"unit\":\"percent\",\"value\":5}", i ? "," : "", i);
    n += (size_t)snprintf(body + n, sizeof(body) - n, "}}}}");
    reject_atomic(body, n);
    const char *bad[] = {
        "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"cpu\":{\"kind\":\"metrics\","
        "\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{\"m\":{\"unit\":\"percent\",\"value\":"
        "101}}}}}",
        "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"cpu\":{\"kind\":\"metrics\","
        "\"status\":\"error\",\"sample_age_ms\":0,\"metrics\":{\"m\":{\"unit\":\"percent\","
        "\"value\":0}}}}}",
        "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"cpu\":{\"kind\":\"metrics\","
        "\"status\":\"ok\",\"sample_age_ms\":0.5,\"metrics\":{}}}}",
        "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"cpu\":{\"kind\":\"metrics\","
        "\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{\"m\":{\"unit\":\"bytes\",\"value\":"
        "9007199254740992}}}}}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
        reject_atomic(bad[i], strlen(bad[i]));
}

static size_t tracked_calls, tracked_fail, tracked_live;
static void *tracked_alloc(size_t size)
{
    if (tracked_calls++ == tracked_fail)
        return NULL;
    void *p = malloc(size);
    if (p)
        tracked_live++;
    return p;
}
static void tracked_free(void *p)
{
    if (p) {
        CHECK(tracked_live > 0);
        tracked_live--;
    }
    free(p);
}
static void test_parse_cleanup(void)
{
    curio_core_destroy(&core);
    curio_core_init_allocator(&core, (curio_allocator_t){tracked_alloc, tracked_free});
    tracked_fail = SIZE_MAX;
    CHECK(apply("host", true, agent, 123) == CURIO_OK);
    const char *body =
        "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"Host\",\"sources\":{\"cpu\":{"
        "\"kind\":\"metrics\",\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{"
        "\"usage\":{\"unit\":\"percent\",\"value\":5}}},\"herdr\":{\"kind\":"
        "\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[{\"id\":\"a\","
        "\"name\":\"A\",\"brand\":\"Codex\",\"project\":\"\",\"state\":\"working\"}]}}}";
    before = core;
    size_t baseline = tracked_live;
    bool completed = false;
    for (tracked_fail = 0; tracked_fail < 200; tracked_fail++) {
        tracked_calls = 0;
        cJSON_Hooks hooks = {tracked_alloc, tracked_free};
        cJSON_InitHooks(&hooks);
        curio_snapshot_t *snapshot = NULL;
        curio_result_t result = curio_core_parse(&core, body, strlen(body), &snapshot);
        cJSON_InitHooks(NULL);
        CHECK(memcmp(&before, &core, sizeof(core)) == 0);
        if (result != CURIO_OK)
            CHECK(snapshot == NULL);
        curio_core_snapshot_free(&core, snapshot);
        CHECK(tracked_live == baseline);
        if (result == CURIO_OK) {
            completed = true;
            break;
        }
    }
    CHECK(completed);
    tracked_fail = SIZE_MAX;
    char excess[2048];
    size_t n =
        (size_t)snprintf(excess, sizeof(excess),
                         "{\"v\":2,\"host_id\":\"host\",\"host_name\":\"Host\",\"sources\":{");
    for (unsigned i = 0; i < 9; i++)
        n += (size_t)snprintf(
            excess + n, sizeof(excess) - n,
            "%s\"s%u\":{\"kind\":\"metrics\",\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{}}",
            i ? "," : "", i);
    n += (size_t)snprintf(excess + n, sizeof(excess) - n, "}}");
    reject_atomic(excess, n);
    CHECK(tracked_live == baseline);
    CHECK(curio_core_apply_json(&core, body, strlen(body), 200) == CURIO_OK);
    curio_snapshot_t *parsed = NULL;
    char *next = snapshot("host", true, agent);
    CHECK(curio_core_parse(&core, next, strlen(next), &parsed) == CURIO_OK);
    free(next);
    tracked_calls = 0;
    tracked_fail = 0;
    CHECK(curio_core_apply(&core, parsed, 300) == CURIO_OK);
    CHECK(tracked_calls == 0);
    CHECK(curio_core_host(&core, "host")->source_count == 1);
    curio_core_snapshot_free(&core, parsed);
    tracked_fail = SIZE_MAX;
    curio_core_destroy(&core);
    CHECK(tracked_live == 0);
    curio_core_init(&core);
}

static void test_brand(void)
{
    curio_core_destroy(&core);
    curio_core_init(&core);
    const char *branded = "{\"id\":\"a\",\"name\":\"Session\",\"brand\":\"Codex品牌\",\"project\":"
                          "\"p\",\"state\":\"working\"}";
    CHECK(apply("brand", true, branded, 100) == CURIO_OK);
    const curio_host_t *h = curio_core_host(&core, "brand");
    CHECK(!strcmp(herdr(h)->agents[0].brand, "Codex品牌"));
    CHECK(!strcmp(herdr(h)->agents[0].name, "Session"));
    char *branded_json = curio_core_state_v2_json(&core, 100);
    CHECK(branded_json && strstr(branded_json, "Codex品牌"));
    curio_core_free_json(branded_json);
    CHECK(apply("brand", false, "", 200) == CURIO_OK);
    CHECK(!strcmp(herdr(h)->agents[0].brand, "Codex品牌"));
    const char *bad[] = {"null", "42", "true", "\"123456789012345678901234567890123\"",
                         "\"品牌品牌品牌品牌品牌品牌\""};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        char a[512];
        snprintf(
            a, sizeof(a),
            "{\"id\":\"a\",\"name\":\"N\",\"brand\":%s,\"project\":\"\",\"state\":\"working\"}",
            bad[i]);
        char *json = snapshot("brand", true, a);
        reject_atomic(json, strlen(json));
        free(json);
    }
    CHECK(apply("brand", true,
                "{\"id\":\"a\",\"name\":\"N\",\"brand\":\"12345678901234567890123456789012\","
                "\"project\":\"\",\"state\":\"idle\"}",
                300) == CURIO_OK);
    CHECK(strlen(herdr(h)->agents[0].brand) == 32);
    CHECK(apply("brand", true, agent, 400) == CURIO_OK);
    CHECK(herdr(h)->agents[0].brand[0] == 0);
    char *json = curio_core_state_v2_json(&core, 400);
    CHECK(json && !strstr(json, "\"brand\":"));
    curio_core_free_json(json);
    const char *v2 =
        "{\"v\":2,\"host_id\":\"brand\",\"host_name\":\"H\",\"sources\":{\"herdr\":{\"kind\":"
        "\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[{\"id\":\"a\",\"name\":\"N\","
        "\"brand\":\"Claude\",\"project\":\"p\",\"state\":\"working\"}]}}}";
    CHECK(curio_core_apply_json(&core, v2, strlen(v2), 500) == CURIO_OK);
    CHECK(!strcmp(herdr(h)->agents[0].brand, "Claude"));
    test_v2_oom();
}

static void test_status_and_format(void)
{
    if (core.allocator.free) curio_core_destroy(&core);
    curio_core_init(&core);
    const char *ok = "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"s\":{\"kind\":\"status\",\"status\":\"ok\",\"max_age_ms\":60000,\"sample_age_ms\":500,\"state\":{\"text\":\"Ready\",\"level\":\"warning\",\"detail\":\"Details\"}}}}";
    CHECK(curio_core_apply_json(&core, ok, strlen(ok), 1000) == CURIO_OK);
    const curio_host_t *h = curio_core_host(&core, "h");
    const curio_source_t *src = curio_host_source(h, "s");
    CHECK(src->state && src->state->level == CURIO_STATUS_WARNING);
    CHECK(curio_host_health(h, 32000) == CURIO_HEALTH_OFFLINE);
    CHECK(curio_source_health(src, 32000) == CURIO_HEALTH_FRESH);
    CHECK(curio_source_health(src, 60500) == CURIO_HEALTH_FRESH);
    CHECK(curio_source_health(src, 60501) == CURIO_HEALTH_STALE);
    curio_status_t *previous = src->state;
    const char *failed = "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"s\":{\"kind\":\"status\",\"status\":\"error\",\"sample_age_ms\":0}}}";
    CHECK(curio_core_apply_json(&core, failed, strlen(failed), 2000) == CURIO_OK);
    src = curio_host_source(h, "s");
    CHECK(src->state == previous && src->sample_age_ms == 1500);
    CHECK(curio_source_health(src, 2000) == CURIO_HEALTH_UNAVAILABLE);
    char *json = curio_core_catalog_json(&core, 2000);
    CHECK(json && strstr(json, "\"max_age_ms\":10000") && !strstr(json, "Ready"));
    curio_core_free_json(json);
    json = curio_core_state_v2_json(&core, 2000);
    CHECK(json && strstr(json, "Ready"));
    curio_core_free_json(json);
    const char *bad_states[] = {"null", "{}", "{\"text\":\"\"}", "{\"text\":\"ok\",\"level\":\"bad\"}", "{\"text\":42}", "{\"text\":\"ok\",\"detail\":false}"};
    char body[1024];
    for (size_t i = 0; i < sizeof(bad_states) / sizeof(*bad_states); i++) {
        snprintf(body, sizeof(body), "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"s\":{\"kind\":\"status\",\"status\":\"ok\",\"sample_age_ms\":0,\"state\":%s}}}", bad_states[i]);
        reject_atomic(body, strlen(body));
    }
    for (oom_fail = 0; oom_fail < 4; oom_fail++) {
        curio_core_t temp;
        curio_core_init_allocator(&temp, (curio_allocator_t){oom_alloc, free});
        oom_calls = 0;
        CHECK(curio_core_apply_json(&temp, ok, strlen(ok), 0) == (oom_fail < 3 ? CURIO_NO_MEMORY : CURIO_OK));
        curio_core_destroy(&temp);
    }
    struct { double value; const char *unit, *expected; } cases[] = {
        {1536, "bytes", "1.5 KiB"}, {1024, "bytes_per_second", "1 KiB/s"},
        {1500, "watts", "1.5 kW"}, {1500, "milliseconds", "1.5 s"},
        {90, "seconds", "1.5 min"}, {42, "percent", "42%"},
        {-0.001, "number", "0"}, {42, "celsius", "42 C"},
        {1e9, "hertz", "1 GHz"}, {2, "count", "2"}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        char out[64];
        curio_format_metric(out, sizeof(out), cases[i].value, cases[i].unit);
        CHECK(!strcmp(out, cases[i].expected));
    }
}

int main(void)
{
    test_status_and_format();
    test_lifecycle();
    test_atomic_validation();
    test_limits();
    test_hosts_and_eviction();
    test_state_and_catalog();
    test_v2();
    test_v2_oom();
    test_v2_limits();
    test_parse_cleanup();
    test_brand();
    printf("curio core: %u checks passed\n", checks);
    curio_core_destroy(&core);
    return 0;
}
