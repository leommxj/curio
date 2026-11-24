#include "config.h"
#include "cJSON.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned emissions, cancellations;
static char last_key[149];
static void emit(void *ctx, const char *key, const curio_alert_notification_t *n)
{
    (void)ctx;
    assert(n->title[0]);
    strcpy(last_key, key);
    emissions++;
}

static void cancel(void *ctx, const char *key)
{
    (void)ctx;
    assert(!strcmp(key,"host"));
    cancellations++;
}

void test_alert_host_offline(void)
{
    const curio_alert_type_t *type = &curio_alert_host_offline;
    host_offline_options_t options;
    cJSON *json = cJSON_Parse("{\"hold\":5}");
    assert(json && type->parse(json, &options));
    cJSON_Delete(json);
    char description[384];
    type->describe(&options, description, sizeof(description));
    assert(strstr(description,"Offline for: 5s"));
    cJSON *defaults = cJSON_Parse("{}"), *written = cJSON_CreateObject();
    host_offline_options_t default_options;
    assert(defaults && written && type->parse(defaults, &default_options));
    assert(type->write(written, &default_options));
    assert(!cJSON_GetObjectItemCaseSensitive(written, "host"));
    assert(!cJSON_GetObjectItemCaseSensitive(written, "hold"));
    cJSON_Delete(defaults);
    cJSON_Delete(written);
    void *state = calloc(1, type->state_size);
    assert(state);
    curio_core_t core = {0};
    core.hosts[0] = (curio_host_t){.occupied=true, .host_id="host"};
    type->evaluate(state, &options, &core, 0, true, emit, cancel, NULL);
    type->evaluate(state, &options, &core, 31000, false, emit, cancel, NULL);
    type->evaluate(state, &options, &core, 40000, true, emit, cancel, NULL);
    type->evaluate(state, &options, &core, 46000, true, emit, cancel, NULL);
    assert(emissions==0);
    core.hosts[0].last_report_ms=47000;
    type->evaluate(state, &options, &core, 47000, true, emit, cancel, NULL);
    type->evaluate(state, &options, &core, 78000, true, emit, cancel, NULL);
    type->evaluate(state, &options, &core, 83000, true, emit, cancel, NULL);
    assert(emissions==1 && !strcmp(last_key,"host"));
    core.hosts[0].last_report_ms=84000;
    type->evaluate(state, &options, &core, 84000, true, emit, cancel, NULL);
    assert(cancellations==3);
    type->evaluate(state, &options, &core, 115000, true, emit, cancel, NULL);
    strcpy(core.hosts[0].host_id, "replacement");
    type->evaluate(state, &options, &core, 120000, true, emit, cancel, NULL);
    assert(emissions == 1);
    strcpy(core.hosts[0].host_id, "host");
    core.hosts[0].last_report_ms = 121000;
    type->evaluate(state, &options, &core, 121000, true, emit, cancel, NULL);
    core.hosts[0].occupied = false;
    type->evaluate(state, &options, &core, 122000, true, emit, cancel, NULL);
    core.hosts[0].occupied = true;
    type->evaluate(state, &options, &core, 160000, true, emit, cancel, NULL);
    type->evaluate(state, &options, &core, 170000, true, emit, cancel, NULL);
    assert(emissions == 1);
    free(state);
}
