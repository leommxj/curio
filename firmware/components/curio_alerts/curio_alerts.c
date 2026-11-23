#include "curio_alerts.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[97];
    curio_agent_state_t state;
    bool seen;
} agent_track_t;

typedef struct {
    char host_id[49];
    bool used, seen_online, accumulating, alarmed;
    uint64_t started_ms;
} host_track_t;

typedef struct {
    host_track_t hosts[CURIO_MAX_HOSTS];
    agent_track_t *agents;
} rule_track_t;

struct curio_alerts {
    curio_allocator_t allocator;
    curio_alert_config_t config;
    rule_track_t *tracks;
    curio_alert_notification_t queue[CURIO_ALERT_QUEUE];
    size_t count;
    uint32_t next_id, dropped;
    uint64_t shown_ms;
};

static uint64_t elapsed(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

static bool identifier(const char *s)
{
    if (!s || !*s)
        return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_' || *s == '-' || *s == '.'))
            return false;
    return true;
}

static bool unit_valid(const char *s)
{
    if (!s || !*s)
        return false;
    for (; *s; s++)
        if ((unsigned char)*s < 33 || (unsigned char)*s > 126)
            return false;
    return true;
}

void curio_alert_config_default(curio_alert_config_t *c)
{
    if (!c)
        return;
    memset(c, 0, sizeof(*c));
    c->volume = 35;
    c->vibration_strength = 60;
}

static bool valid_rule(const curio_alert_rule_t *r)
{
    if (!identifier(r->id) || r->display_s > 300 ||
        (r->type != CURIO_ALERT_AGENT_DONE && r->type != CURIO_ALERT_METRIC_HIGH &&
         r->type != CURIO_ALERT_HOST_OFFLINE) ||
        (!identifier(r->host_id) && strcmp(r->host_id, "*")))
        return false;
    if (r->type == CURIO_ALERT_AGENT_DONE)
        return !strcmp(r->source_id, "herdr");
    if (r->type == CURIO_ALERT_HOST_OFFLINE)
        return r->hold_s >= 5 && r->hold_s <= 86400;
    return strcmp(r->host_id, "*") && identifier(r->source_id) &&
           identifier(r->key) && unit_valid(r->unit) && strlen(r->unit) <= 16 &&
           r->hold_s >= 1 && r->hold_s <= 86400 && isfinite(r->threshold) &&
           isfinite(r->recover) && r->recover < r->threshold;
}

bool curio_alert_config_validate(const curio_alert_config_t *c)
{
    if (!c || c->volume > 100 || c->vibration_strength > 100 || c->rule_count > CURIO_ALERT_MAX_RULES)
        return false;
    for (size_t i = 0; i < c->rule_count; i++) {
        if (!valid_rule(&c->rules[i]))
            return false;
        for (size_t j = 0; j < i; j++)
            if (!strcmp(c->rules[i].id, c->rules[j].id))
                return false;
    }
    return true;
}

curio_alerts_t *curio_alerts_create(curio_allocator_t allocator)
{
    if (!allocator.alloc || !allocator.free)
        return NULL;
    curio_alerts_t *a = allocator.alloc(sizeof(*a));
    if (!a)
        return NULL;
    memset(a, 0, sizeof(*a));
    a->allocator = allocator;
    curio_alert_config_default(&a->config);
    return a;
}

void curio_alerts_destroy(curio_alerts_t *a)
{
    if (!a)
        return;
    if (a->tracks)
        for (size_t i = 0; i < a->config.rule_count; i++)
            a->allocator.free(a->tracks[i].agents);
    a->allocator.free(a->tracks);
    a->allocator.free(a);
}

uint32_t curio_alerts_revision(const curio_alerts_t *a)
{
    return a ? a->config.revision : 0;
}

void curio_alerts_set_vibration_strength(curio_alerts_t *a, uint8_t strength)
{
    if (!a || strength > 100) return;
    a->config.vibration_strength = strength;
    for (size_t i = 0; i < a->count; i++) a->queue[i].vibration_strength = strength;
}

void curio_alerts_set_volume(curio_alerts_t *a, uint8_t volume)
{
    if (!a || volume > 100) return;
    a->config.volume = volume;
    for (size_t i = 0; i < a->count; i++) a->queue[i].volume = volume;
}

bool curio_alerts_configure(curio_alerts_t *a, const curio_alert_config_t *c)
{
    if (!a || !curio_alert_config_validate(c))
        return false;
    bool enabled = false;
    for (size_t i = 0; i < c->rule_count; i++)
        enabled |= c->rules[i].enabled;
    rule_track_t *tracks = NULL;
    if (enabled) {
        tracks = a->allocator.alloc(c->rule_count * sizeof(*tracks));
        if (!tracks)
            return false;
        memset(tracks, 0, c->rule_count * sizeof(*tracks));
        for (size_t i = 0; i < c->rule_count; i++) {
            if (!c->rules[i].enabled || c->rules[i].type != CURIO_ALERT_AGENT_DONE)
                continue;
            tracks[i].agents = a->allocator.alloc(CURIO_MAX_HOSTS * CURIO_MAX_AGENTS *
                                                  sizeof(agent_track_t));
            if (!tracks[i].agents) {
                for (size_t j = 0; j < i; j++)
                    a->allocator.free(tracks[j].agents);
                a->allocator.free(tracks);
                return false;
            }
            memset(tracks[i].agents, 0,
                   CURIO_MAX_HOSTS * CURIO_MAX_AGENTS * sizeof(agent_track_t));
        }
    }
    if (a->tracks)
        for (size_t i = 0; i < a->config.rule_count; i++)
            a->allocator.free(a->tracks[i].agents);
    a->allocator.free(a->tracks);
    a->tracks = tracks;
    a->config = *c;
    a->count = 0;
    a->shown_ms = 0;
    return true;
}

static host_track_t *track_for(rule_track_t *r, const char *id)
{
    host_track_t *empty = NULL;
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++) {
        if (r->hosts[i].used && !strcmp(r->hosts[i].host_id, id))
            return &r->hosts[i];
        if (!r->hosts[i].used)
            empty = &r->hosts[i];
    }
    if (!empty)
        return NULL;
    empty->used = true;
    strcpy(empty->host_id, id);
    return empty;
}

static void pop(curio_alerts_t *a, uint64_t now)
{
    if (!a->count)
        return;
    memmove(a->queue, a->queue + 1, (--a->count) * sizeof(a->queue[0]));
    a->shown_ms = now;
}

static void invalidate_pending(curio_alerts_t *a, const char *rule, const char *host)
{
    for (size_t i = 1; i < a->count;) {
        if (!strcmp(a->queue[i].rule_id, rule) && !strcmp(a->queue[i].host_id, host)) {
            memmove(a->queue + i, a->queue + i + 1, (--a->count - i) * sizeof(a->queue[0]));
        } else
            i++;
    }
}

static void enqueue(curio_alerts_t *a, const curio_alert_rule_t *r, const curio_host_t *h,
                    const curio_agent_t *agent, double value, uint64_t now)
{
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->queue[i].rule_id, r->id) &&
            !strcmp(a->queue[i].host_id, h->host_id) &&
            !strcmp(a->queue[i].agent_id, agent ? agent->id : ""))
            return;
    curio_alert_notification_t n = {0};
    n.id = ++a->next_id;
    n.kind = r->type == CURIO_ALERT_AGENT_DONE ? CURIO_ALERT_INFO : CURIO_ALERT_WARNING;
    strcpy(n.rule_id, r->id);
    strcpy(n.host_id, h->host_id);
    if (agent)
        strcpy(n.agent_id, agent->id);
    n.sound = r->sound;
    n.vibrate = r->vibrate;
    n.flash = r->flash;
    n.vibration_strength = a->config.vibration_strength;
    n.display_s = r->display_s;
    n.volume = a->config.volume;
    if (r->type == CURIO_ALERT_AGENT_DONE) {
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
    } else if (r->type == CURIO_ALERT_METRIC_HIGH) {
        strcpy(n.title, "Metric high");
        snprintf(n.body, sizeof(n.body), "%s", h->host_name);
        char formatted[64];
        curio_format_metric(formatted, sizeof(formatted), value, r->unit);
        snprintf(n.detail, sizeof(n.detail), "%s/%s: %.29s", r->source_id, r->key, formatted);
    } else {
        strcpy(n.title, "Host offline");
        snprintf(n.body, sizeof(n.body), "%s", h->host_name);
        snprintf(n.detail, sizeof(n.detail), "%s", h->host_id);
    }
    if (a->count == CURIO_ALERT_QUEUE) {
        a->dropped++;
        return;
    }
    size_t index = a->count;
    if (n.kind == CURIO_ALERT_WARNING)
        for (size_t i = a->count ? 1 : 0; i < a->count; i++)
            if (a->queue[i].kind == CURIO_ALERT_INFO) {
                index = i;
                break;
            }
    memmove(a->queue + index + 1, a->queue + index,
            (a->count - index) * sizeof(a->queue[0]));
    a->queue[index] = n;
    a->count++;
    if (a->count == 1)
        a->shown_ms = now;
}

static void tick_agents(curio_alerts_t *a, rule_track_t *rt, const curio_alert_rule_t *r,
                        const curio_host_t *h, uint64_t now)
{
    host_track_t *t = track_for(rt, h->host_id);
    if (!t)
        return;
    agent_track_t *agents = rt->agents + (t - rt->hosts) * CURIO_MAX_AGENTS;
    const curio_source_t *s = curio_host_source(h, r->source_id);
    if (curio_host_health(h, now) == CURIO_HEALTH_OFFLINE || !s || s->kind != CURIO_SOURCE_AGENTS || curio_source_health(s, now) != CURIO_HEALTH_FRESH) {
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
                enqueue(a, r, h, agent, 0, now);
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

static void tick_metric(curio_alerts_t *a, rule_track_t *rt, const curio_alert_rule_t *r,
                        const curio_host_t *h, uint64_t now)
{
    host_track_t *t = track_for(rt, h->host_id);
    if (!t)
        return;
    const curio_source_t *s = curio_host_source(h, r->source_id);
    const curio_metric_t *m = curio_source_metric(s, r->key);
    if (!s || s->kind != CURIO_SOURCE_METRICS ||
        curio_host_health(h, now) == CURIO_HEALTH_OFFLINE ||
        curio_source_health(s, now) != CURIO_HEALTH_FRESH || !m || !m->has_value ||
        curio_metric_age(m, now) > s->max_age_ms || strcmp(m->unit, r->unit) ||
        !isfinite(m->value)) {
        t->accumulating = false;
        return;
    }
    if (m->value <= r->recover) {
        t->accumulating = false;
        t->alarmed = false;
        invalidate_pending(a, r->id, h->host_id);
    } else if (m->value > r->threshold && !t->alarmed) {
        if (!t->accumulating) {
            t->accumulating = true;
            t->started_ms = now;
        } else if (elapsed(now, t->started_ms) >= (uint64_t)r->hold_s * 1000) {
            t->alarmed = true;
            t->accumulating = false;
            enqueue(a, r, h, NULL, m->value, now);
        }
    } else
        t->accumulating = false;
}

static void tick_offline(curio_alerts_t *a, rule_track_t *rt, const curio_alert_rule_t *r,
                         const curio_host_t *h, uint64_t now, bool wifi)
{
    host_track_t *t = track_for(rt, h->host_id);
    if (!t)
        return;
    if (!wifi) {
        t->seen_online = false;
        t->accumulating = false;
        return;
    }
    bool online = elapsed(now, h->last_report_ms) <= CURIO_OFFLINE_MS;
    if (online) {
        t->seen_online = true;
        t->accumulating = false;
        t->alarmed = false;
        invalidate_pending(a, r->id, h->host_id);
    } else if (t->seen_online && !t->alarmed) {
        if (!t->accumulating) {
            t->accumulating = true;
            t->started_ms = now;
        } else if (elapsed(now, t->started_ms) >= (uint64_t)r->hold_s * 1000) {
            t->accumulating = false;
            t->alarmed = true;
            enqueue(a, r, h, NULL, 0, now);
        }
    }
}

void curio_alerts_tick(curio_alerts_t *a, const curio_core_t *core, uint64_t now, bool wifi)
{
    if (!a || !core || !a->tracks)
        return;
    if (a->count && a->queue[0].display_s &&
        elapsed(now, a->shown_ms) >= (uint64_t)a->queue[0].display_s * 1000)
        pop(a, now);
    for (size_t i = 0; i < a->config.rule_count; i++) {
        const curio_alert_rule_t *r = &a->config.rules[i];
        if (!r->enabled)
            continue;
        for (size_t j = 0; j < CURIO_MAX_HOSTS; j++) {
            host_track_t *t = &a->tracks[i].hosts[j];
            if (t->used && !curio_core_host(core, t->host_id)) {
                memset(t, 0, sizeof(*t));
                if (a->tracks[i].agents)
                    memset(a->tracks[i].agents + j * CURIO_MAX_AGENTS, 0,
                           CURIO_MAX_AGENTS * sizeof(agent_track_t));
            }
        }
        for (size_t j = 0; j < CURIO_MAX_HOSTS; j++) {
            const curio_host_t *h = &core->hosts[j];
            if (!h->occupied || (strcmp(r->host_id, "*") && strcmp(r->host_id, h->host_id)))
                continue;
            if (r->type == CURIO_ALERT_AGENT_DONE)
                tick_agents(a, &a->tracks[i], r, h, now);
            else if (r->type == CURIO_ALERT_METRIC_HIGH)
                tick_metric(a, &a->tracks[i], r, h, now);
            else
                tick_offline(a, &a->tracks[i], r, h, now, wifi);
        }
    }
}

const curio_alert_notification_t *curio_alerts_current(const curio_alerts_t *a, uint64_t now)
{
    if (!a || !a->count || (a->queue[0].display_s &&
        elapsed(now, a->shown_ms) >= (uint64_t)a->queue[0].display_s * 1000))
        return NULL;
    return &a->queue[0];
}

uint64_t curio_alerts_remaining_ms(const curio_alerts_t *a, uint64_t now)
{
    if (!a || !a->count || !a->queue[0].display_s) return 0;
    uint64_t duration = (uint64_t)a->queue[0].display_s * 1000;
    uint64_t age = elapsed(now, a->shown_ms);
    return age < duration ? duration - age : 0;
}

void curio_alerts_dismiss(curio_alerts_t *a, uint64_t now)
{
    if (a)
        pop(a, now);
}

size_t curio_alerts_pending(const curio_alerts_t *a)
{
    return a ? a->count : 0;
}

uint32_t curio_alerts_dropped(const curio_alerts_t *a)
{
    return a ? a->dropped : 0;
}
