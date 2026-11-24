#include "config.h"
#include "curio_module.h"
#include <stdio.h>
#include <string.h>

static bool validate(const void *options)
{
    const agent_done_options_t *r = options;
    if ((!curio_identifier(r->host_id, 48) && strcmp(r->host_id,"*"))) return false;
    return curio_identifier(r->source_id, 48);
}

static bool parse(const cJSON *json, void *options)
{
    agent_done_options_t *r = options;
    memset(r, 0, sizeof(*r));
    if (!curio_string(json,"host", r->host_id, sizeof(r->host_id),"*")) return false;
    if (!curio_string(json,"source", r->source_id, sizeof(r->source_id),"herdr") ||
        !curio_boolean(json,"include_idle", true, &r->include_idle)) return false;
    return validate(r);
}

static bool write(cJSON *json, const void *options)
{
    const agent_done_options_t *r = options;
    return (!strcmp(r->host_id, "*") || curio_put_string(json, "host", r->host_id)) &&
        (!strcmp(r->source_id, "herdr") || curio_put_string(json, "source", r->source_id)) &&
            (r->include_idle || curio_put_bool(json,"include_idle", false));
}

static void describe(const void *options, char *out, size_t size)
{
    const agent_done_options_t *r = options;
    snprintf(out, size, "Host: %s\nSource: %s\nState: Working -> %s",
        strcmp(r->host_id, "*") ? r->host_id : "All hosts", r->source_id,
        r->include_idle ? "Done / Idle" : "Done");
}

typedef struct {
    char id[97];
    curio_agent_state_t state;
    bool seen;
} agent_track_t;

typedef struct {
    char host_id[49];
    agent_track_t agents[CURIO_MAX_AGENTS];
} host_track_t;

typedef struct {
    host_track_t hosts[CURIO_MAX_HOSTS];
} rule_track_t;

static void notify(const curio_host_t *h, const curio_agent_t *agent,
                   curio_alert_emit_fn emit, void *ctx)
{
    curio_alert_notification_t n = {0};
    n.kind = CURIO_ALERT_INFO;
    strcpy(n.host_id, h->host_id);
    strcpy(n.agent_id, agent->id);

    const char *name = agent->brand[0] && strcmp(agent->name, agent->brand)
                           ? agent->name
                           : agent->project[0] ? agent->project : agent->name;
    const char *brand = agent->brand[0] ? agent->brand : agent->name;
    const char *project = agent->project[0] && strcmp(name, agent->project)
                              ? agent->project
                              : NULL;
    strcpy(n.title, "Agent finished");
    snprintf(n.body, sizeof(n.body), "%s", name);
    snprintf(n.detail, sizeof(n.detail), "%s | %s", brand, h->host_name);
    if (project && strlen(n.detail) + 3 + strlen(project) < sizeof(n.detail)) {
        strcat(n.detail, " | ");
        strcat(n.detail, project);
    }

    char key[149];
    snprintf(key, sizeof(key), "%s/%s", h->host_id, agent->id);
    emit(ctx, key, &n);
}

static void tick_agents(agent_track_t *agents, const agent_done_options_t *r,
                        const curio_host_t *h, uint64_t now, curio_alert_emit_fn emit, void *ctx)
{
    const curio_source_t *s = curio_host_source(h, r->source_id);
    if (curio_host_health(h, now) == CURIO_HEALTH_OFFLINE || !s || s->kind != CURIO_SOURCE_AGENTS ||
        curio_source_health(s, now) != CURIO_HEALTH_FRESH) {
        memset(agents, 0, CURIO_MAX_AGENTS * sizeof(*agents));
        return;
    }
    for (size_t k = 0; k < CURIO_MAX_AGENTS; k++) {
        if (!agents[k].seen) continue;
        bool present = false;
        for (size_t j = 0; j < s->agent_count; j++)
            present |= !strcmp(agents[k].id, s->agents[j].id);
        if (!present) agents[k].seen = false;
    }
    for (size_t j = 0; j < s->agent_count; j++) {
        const curio_agent_t *agent = &s->agents[j];
        agent_track_t *entry = NULL;
        for (size_t k = 0; k < CURIO_MAX_AGENTS; k++)
            if (agents[k].seen && !strcmp(agents[k].id, agent->id)) {
                entry = &agents[k];
                break;
            }
        if (entry) {
            bool complete = entry->state == CURIO_STATE_WORKING &&
                            (agent->state == CURIO_STATE_DONE ||
                             (r->include_idle && agent->state == CURIO_STATE_IDLE));
            if (complete)
                notify(h, agent, emit, ctx);
            entry->state = agent->state;
        } else
            for (size_t k = 0; k < CURIO_MAX_AGENTS; k++)
                if (!agents[k].seen) {
                    strcpy(agents[k].id, agent->id);
                    agents[k].state = agent->state;
                    agents[k].seen = true;
                    break;
                }
    }
}

static void evaluate(void *state, const void *options, const curio_core_t *core,
                     uint64_t now, bool wifi, curio_alert_emit_fn emit,
                     curio_alert_cancel_fn cancel, void *ctx)
{
    (void)wifi;
    (void)cancel;
    rule_track_t *rt = state;
    const agent_done_options_t *r = options;
    for (size_t j = 0; j < CURIO_MAX_HOSTS; j++) {
        const curio_host_t *h = &core->hosts[j];
        host_track_t *t = &rt->hosts[j];
        if (!h->occupied || strcmp(t->host_id, h->host_id)) {
            memset(t, 0, sizeof(*t));
            if (h->occupied) strcpy(t->host_id, h->host_id);
        }
        if (!h->occupied || (strcmp(r->host_id, "*") && strcmp(r->host_id, h->host_id))) continue;
        tick_agents(t->agents, r, h, now, emit, ctx);
    }
}

const curio_alert_type_t curio_alert_agent_done = {
    .id = "agent_done",
    .title = "Agent finished",
    .options_size = sizeof(agent_done_options_t),
    .fields = "host|source|include_idle",
    .parse = parse,
    .validate = validate,
    .write = write,
    .describe = describe,
    .state_size = sizeof(rule_track_t),
    .evaluate = evaluate
};
