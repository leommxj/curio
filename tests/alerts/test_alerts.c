#include "curio_alerts.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int fail_alloc;
static void *test_alloc(size_t size)
{
    return fail_alloc && --fail_alloc == 0 ? NULL : malloc(size);
}

static curio_alert_config_t config(void)
{
    curio_alert_config_t c;
    curio_alert_config_default(&c);
    c.rule_count = 3;
    curio_alert_rule_t *r = &c.rules[0];
    strcpy(r->id, "agent");
    r->enabled = true;
    r->type = CURIO_ALERT_AGENT_DONE;
    strcpy(r->host_id, "*");
    strcpy(r->source_id, "herdr");
    r->include_idle = true;
    r->vibrate = true;
    r->flash = true;
    r->display_s = 8;
    r = &c.rules[1];
    strcpy(r->id, "metric");
    r->enabled = true;
    r->type = CURIO_ALERT_METRIC_HIGH;
    strcpy(r->host_id, "host");
    strcpy(r->source_id, "system.memory");
    strcpy(r->key, "usage_pct");
    strcpy(r->unit, "percent");
    r->threshold = 90;
    r->recover = 85;
    r->hold_s = 5;
    r->display_s = 8;
    r = &c.rules[2];
    strcpy(r->id, "offline");
    r->enabled = true;
    r->type = CURIO_ALERT_HOST_OFFLINE;
    strcpy(r->host_id, "*");
    r->hold_s = 5;
    r->display_s = 8;
    return c;
}

static void test_config(void)
{
    curio_alert_config_t c;
    curio_alert_config_default(&c);
    assert(curio_alert_config_validate(&c));
    assert(c.rule_count == 0 && c.volume == 35 && c.vibration_strength == 60);
    c = config();
    assert(curio_alert_config_validate(&c));
    c.rules[0].display_s = 0;
    assert(curio_alert_config_validate(&c));
    c.rules[0].display_s = 301;
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.rules[1].recover = c.rules[1].threshold;
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.rules[1].threshold = NAN;
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.rules[2].hold_s = 4;
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.rules[1].hold_s = 0;
    assert(!curio_alert_config_validate(&c));
    c = config();
    strcpy(c.rules[1].host_id, "*");
    assert(!curio_alert_config_validate(&c));
    c = config();
    strcpy(c.rules[0].source_id, "other");
    assert(!curio_alert_config_validate(&c));
    c = config();
    strcpy(c.rules[2].id, c.rules[0].id);
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.vibration_strength = 101;
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.volume = 101;
    assert(!curio_alert_config_validate(&c));
    c = config();
    c.rule_count = CURIO_ALERT_MAX_RULES + 1;
    assert(!curio_alert_config_validate(&c));
    assert(!curio_alert_config_validate(NULL));
}

static void test_runtime(void)
{
    curio_alert_config_t c = config();
    c.revision = 7;
    curio_alerts_t *a = curio_alerts_create((curio_allocator_t){test_alloc, free});
    assert(a && curio_alerts_configure(a, &c));
    assert(curio_alerts_revision(a) == 7 && curio_alerts_revision(NULL) == 0);
    fail_alloc = 1;
    assert(!curio_alerts_configure(a, &c));
    fail_alloc = 2;
    assert(!curio_alerts_configure(a, &c));
    fail_alloc = 0;

    curio_agent_t agent = {0};
    strcpy(agent.id, "one");
    strcpy(agent.name, "codex");
    strcpy(agent.brand, "codex");
    strcpy(agent.project, "Interesting Project");
    agent.state = CURIO_STATE_DONE;
    curio_metric_t metric = {0};
    strcpy(metric.key, "usage_pct");
    strcpy(metric.unit, "percent");
    metric.has_value = true;
    metric.value = 80;
    curio_source_t source[2] = {0};
    source[0].max_age_ms = source[1].max_age_ms = CURIO_STALE_MS;
    strcpy(source[0].id, "herdr");
    source[0].kind = CURIO_SOURCE_AGENTS;
    source[0].status = CURIO_SOURCE_OK;
    source[0].has_success = true;
    source[0].agents = &agent;
    source[0].agent_count = 1;
    strcpy(source[1].id, "system.memory");
    source[1].kind = CURIO_SOURCE_METRICS;
    source[1].status = CURIO_SOURCE_OK;
    source[1].has_success = true;
    source[1].metrics = &metric;
    source[1].metric_count = 1;
    curio_core_t core = {0};
    curio_host_t *h = &core.hosts[0];
    h->occupied = true;
    strcpy(h->host_id, "host");
    strcpy(h->host_name, "Office host");
    h->sources = source;
    h->source_count = 2;
    h->last_report_ms = 1000;
    source[0].received_ms = source[1].received_ms = metric.received_ms = 1000;
    curio_alerts_tick(a, &core, 1000, true);
    assert(!curio_alerts_current(a, 1000));

    agent.state = CURIO_STATE_WORKING;
    curio_alerts_tick(a, &core, 2000, true);
    agent.state = CURIO_STATE_DONE;
    curio_alerts_tick(a, &core, 3000, true);
    const curio_alert_notification_t *n = curio_alerts_current(a, 3000);
    assert(n && n->kind == CURIO_ALERT_INFO && !strcmp(n->body, "Interesting Project"));
    assert(!strcmp(n->detail, "codex | Office host"));
    assert(curio_alerts_remaining_ms(a, 3000) == 8000);
    assert(curio_alerts_remaining_ms(a, 4000) == 7000);
    assert(curio_alerts_remaining_ms(a, 10999) == 1);
    assert(curio_alerts_remaining_ms(a, 11000) == 0);
    assert(n->flash && n->vibrate && !n->sound && n->vibration_strength == 60);
    uint32_t first = n->id;
    curio_alerts_set_vibration_strength(a, 0);
    assert(curio_alerts_current(a, 3000)->vibration_strength == 0);
    curio_alerts_set_vibration_strength(a, 79);
    curio_alerts_set_vibration_strength(a, 101);
    assert(curio_alerts_current(a, 3000)->vibration_strength == 79);
    assert(curio_alerts_current(a, 3000)->id == first && curio_alerts_revision(a) == 7);
    curio_alerts_set_volume(a, 67);
    assert(curio_alerts_remaining_ms(a, 4000) == 7000);
    assert(curio_alerts_revision(a) == 7);
    n = curio_alerts_current(a, 3000);
    assert(n && n->id == first && n->volume == 67 && curio_alerts_pending(a) == 1);
    curio_alerts_set_volume(a, 101);
    assert(curio_alerts_current(a, 3000)->volume == 67);
    curio_alerts_dismiss(a, 3000);
    assert(curio_alerts_remaining_ms(a, 3000) == 0);
    assert(curio_alerts_remaining_ms(NULL, 3000) == 0);
    curio_alerts_tick(a, &core, 4000, true);
    assert(!curio_alerts_current(a, 4000));
    strcpy(agent.name, "Agent One");
    agent.state = CURIO_STATE_WORKING;
    curio_alerts_tick(a, &core, 4500, true);
    agent.state = CURIO_STATE_DONE;
    curio_alerts_tick(a, &core, 4600, true);
    n = curio_alerts_current(a, 4600);
    assert(n && n->volume == 67 && n->vibrate && !n->sound && n->vibration_strength == 79 && !strcmp(n->body, "Agent One"));
    assert(!strcmp(n->detail, "codex | Office host | Interesting Project"));
    curio_alerts_dismiss(a, 4600);

    metric.value = 95;
    source[1].received_ms = metric.received_ms = h->last_report_ms = 5000;
    curio_alerts_tick(a, &core, 5000, true);
    curio_alerts_tick(a, &core, 9000, true);
    assert(!curio_alerts_current(a, 9000));
    source[1].received_ms = metric.received_ms = h->last_report_ms = 10000;
    curio_alerts_tick(a, &core, 10000, true);
    n = curio_alerts_current(a, 10000);
    assert(n && n->kind == CURIO_ALERT_WARNING && n->id > first);
    assert(!strcmp(n->detail, "system.memory/usage_pct: 95%"));
    curio_alerts_dismiss(a, 10000);
    curio_alerts_tick(a, &core, 11000, true);
    assert(!curio_alerts_current(a, 11000));
    metric.value = 84;
    source[1].received_ms = metric.received_ms = h->last_report_ms = 12000;
    curio_alerts_tick(a, &core, 12000, true);
    metric.value = 96;
    source[1].received_ms = metric.received_ms = h->last_report_ms = 13000;
    curio_alerts_tick(a, &core, 13000, true);
    source[1].status = CURIO_SOURCE_ERROR;
    curio_alerts_tick(a, &core, 15000, true);
    source[1].status = CURIO_SOURCE_OK;
    source[1].received_ms = metric.received_ms = h->last_report_ms = 16000;
    curio_alerts_tick(a, &core, 16000, true);
    assert(!curio_alerts_current(a, 16000));
    source[1].received_ms = metric.received_ms = h->last_report_ms = 21000;
    curio_alerts_tick(a, &core, 21000, true);
    assert(curio_alerts_current(a, 21000));

    agent.state = CURIO_STATE_WORKING;
    source[0].received_ms = 21000;
    curio_alerts_tick(a, &core, 21000, true);
    agent.state = CURIO_STATE_IDLE;
    source[0].received_ms = 22000;
    curio_alerts_tick(a, &core, 22000, true);
    assert(curio_alerts_pending(a) == 2);
    curio_alerts_set_vibration_strength(a, 44);
    assert(curio_alerts_current(a, 22000)->vibration_strength == 44);
    metric.value = 80;
    source[1].received_ms = metric.received_ms = h->last_report_ms = 23000;
    curio_alerts_tick(a, &core, 23000, true);
    assert(curio_alerts_pending(a) == 2);
    curio_alerts_tick(a, &core, 31000, true);
    assert(curio_alerts_current(a, 31000)->kind == CURIO_ALERT_INFO);
    assert(curio_alerts_current(a, 31000)->vibration_strength == 44);
    assert(curio_alerts_remaining_ms(a, 31000) == 8000);
    curio_alerts_tick(a, &core, 32000, true);
    assert(curio_alerts_pending(a) == 1);
    curio_alerts_dismiss(a, 32000);

    h->last_report_ms = 40000;
    source[0].received_ms = source[1].received_ms = metric.received_ms = 40000;
    curio_alerts_tick(a, &core, 40000, true);
    curio_alerts_tick(a, &core, 71000, false);
    curio_alerts_tick(a, &core, 80000, true);
    curio_alerts_tick(a, &core, 86000, true);
    assert(!curio_alerts_current(a, 86000));
    h->last_report_ms = 87000;
    curio_alerts_tick(a, &core, 87000, true);
    curio_alerts_tick(a, &core, 118000, true);
    assert(!curio_alerts_current(a, 118000));
    curio_alerts_tick(a, &core, 123000, true);
    assert(curio_alerts_current(a, 123000));
    c.rules[2].display_s = 0;
    assert(curio_alerts_configure(a, &c));
    h->last_report_ms = 124000;
    curio_alerts_tick(a, &core, 124000, true);
    curio_alerts_tick(a, &core, 155000, true);
    curio_alerts_tick(a, &core, 160000, true);
    assert(curio_alerts_current(a, 160000));
    assert(curio_alerts_remaining_ms(a, 160000) == 0);
    assert(curio_alerts_current(a, 900000));
    curio_alerts_destroy(a);
}

static void test_agent_replacement(void)
{
    curio_alert_config_t c = config();
    c.rule_count = 1;
    curio_alerts_t *a = curio_alerts_create((curio_allocator_t){malloc, free});
    assert(a && curio_alerts_configure(a, &c));
    curio_agent_t agents[CURIO_MAX_AGENTS] = {0};
    for (size_t i = 0; i < CURIO_MAX_AGENTS; i++) {
        agents[i].id[0] = (char)('a' + i);
        strcpy(agents[i].name, "Agent");
        agents[i].state = CURIO_STATE_WORKING;
    }
    curio_source_t source = {.kind = CURIO_SOURCE_AGENTS, .status = CURIO_SOURCE_OK,
        .has_success = true, .max_age_ms = CURIO_STALE_MS, .agents = agents, .agent_count = CURIO_MAX_AGENTS};
    strcpy(source.id, "herdr");
    curio_core_t core = {0};
    core.hosts[0].occupied = true;
    strcpy(core.hosts[0].host_id, "host");
    core.hosts[0].sources = &source;
    core.hosts[0].source_count = 1;
    curio_alerts_tick(a, &core, 0, true);
    strcpy(agents[0].id, "replacement");
    curio_alerts_tick(a, &core, 1, true);
    agents[0].state = CURIO_STATE_DONE;
    curio_alerts_tick(a, &core, 2, true);
    const curio_alert_notification_t *n = curio_alerts_current(a, 2);
    assert(n && !strcmp(n->agent_id, "replacement"));
    curio_alerts_destroy(a);
}

int main(void)
{
    test_config();
    test_runtime();
    test_agent_replacement();
}
