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

void test_alert_metric_high(void)
{
    const curio_alert_type_t *type = &curio_alert_metric_high;
    metric_high_options_t options;
    cJSON *json = cJSON_Parse("{\"host\":\"host\",\"source\":\"metrics\",\"key\":\"load\","
        "\"unit\":\"percent\",\"threshold\":90,\"recover\":80,\"hold\":1}");
    assert(json && type->parse(json, &options));
    cJSON_Delete(json);
    char description[384];
    type->describe(&options, description, sizeof(description));
    assert(strstr(description,"Recover: <= 80%"));
    void *state = calloc(1, type->state_size);
    assert(state);
    curio_core_t core = {0};
    core.hosts[0] = (curio_host_t){.occupied=true, .host_id="host"};
    curio_metric_t metric = {.key="load", .unit="percent", .value=95, .has_value=true};
    curio_source_t source = {.id="metrics", .kind=CURIO_SOURCE_METRICS, .status=CURIO_SOURCE_OK,
        .has_success=true, .max_age_ms=10000, .metrics=&metric, .metric_count=1};
    core.hosts[0].sources=&source;
    core.hosts[0].source_count=1;
    type->evaluate(state, &options, &core, 0, true, emit, cancel, NULL);
    core.hosts[0].occupied=false;
    type->evaluate(state, &options, &core, 500, true, emit, cancel, NULL);
    core.hosts[0].occupied=true;
    type->evaluate(state, &options, &core, 1000, true, emit, cancel, NULL);
    assert(emissions==0);
    type->evaluate(state, &options, &core, 2000, true, emit, cancel, NULL);
    assert(emissions==1 && !strcmp(last_key,"host"));
    metric.value=80;
    type->evaluate(state, &options, &core, 2001, true, emit, cancel, NULL);
    assert(cancellations==1);
    free(state);
}
