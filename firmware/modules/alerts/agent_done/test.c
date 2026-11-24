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

void test_alert_agent_done(void)
{
    const curio_alert_type_t *type = &curio_alert_agent_done;
    agent_done_options_t options;
    cJSON *json = cJSON_Parse("{\"source\":\"custom.agents\"}");
    assert(json && type->parse(json, &options));
    cJSON_Delete(json);
    char description[384];
    type->describe(&options, description, sizeof(description));
    assert(strstr(description,"State: Working -> Done / Idle"));
    cJSON *defaults = cJSON_Parse("{}"), *written = cJSON_CreateObject();
    agent_done_options_t default_options;
    assert(defaults && written && type->parse(defaults, &default_options));
    assert(type->write(written, &default_options));
    assert(!cJSON_GetObjectItemCaseSensitive(written, "host"));
    assert(!cJSON_GetObjectItemCaseSensitive(written, "source"));
    assert(!cJSON_GetObjectItemCaseSensitive(written, "include_idle"));
    cJSON_Delete(defaults);
    cJSON_Delete(written);
    void *state = calloc(1, type->state_size);
    assert(state);
    curio_core_t core = {0};
    core.hosts[0] = (curio_host_t){.occupied=true, .host_id="host"};
    curio_agent_t agents[2] = {{.id="a/b", .state=CURIO_STATE_WORKING}, {.id="b", .state=CURIO_STATE_WORKING}};
    curio_source_t source = {.id="custom.agents", .kind=CURIO_SOURCE_AGENTS, .status=CURIO_SOURCE_OK,
        .has_success=true, .max_age_ms=10000, .agents=agents, .agent_count=2};
    core.hosts[0].sources=&source;
    core.hosts[0].source_count=1;
    type->evaluate(state, &options, &core, 0, true, emit, cancel, NULL);
    agents[0].state=CURIO_STATE_DONE;
    agents[1].state=CURIO_STATE_IDLE;
    type->evaluate(state, &options, &core, 1, true, emit, cancel, NULL);
    assert(emissions==2 && !strcmp(last_key,"host/b"));
    type->evaluate(state, &options, &core, 2, true, emit, cancel, NULL);
    assert(emissions==2 && cancellations==0);
    agents[0].state = CURIO_STATE_WORKING;
    type->evaluate(state, &options, &core, 3, true, emit, cancel, NULL);
    strcpy(core.hosts[0].host_id, "replacement");
    agents[0].state = CURIO_STATE_DONE;
    type->evaluate(state, &options, &core, 4, true, emit, cancel, NULL);
    assert(emissions == 2);
    agents[0].state = CURIO_STATE_WORKING;
    type->evaluate(state, &options, &core, 5, true, emit, cancel, NULL);
    agents[0].state = CURIO_STATE_DONE;
    type->evaluate(state, &options, &core, 6, true, emit, cancel, NULL);
    assert(emissions == 3 && !strcmp(last_key, "replacement/a/b"));
    agents[0].state = CURIO_STATE_WORKING;
    type->evaluate(state, &options, &core, 7, true, emit, cancel, NULL);
    core.hosts[0].occupied = false;
    type->evaluate(state, &options, &core, 8, true, emit, cancel, NULL);
    core.hosts[0].occupied = true;
    agents[0].state = CURIO_STATE_DONE;
    type->evaluate(state, &options, &core, 9, true, emit, cancel, NULL);
    assert(emissions == 3);
    free(state);
}
