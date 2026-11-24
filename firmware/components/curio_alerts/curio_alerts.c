#include "curio_alerts.h"
#include "cJSON.h"
#include "curio_module.h"
#include <string.h>
struct curio_alerts {
    curio_allocator_t allocator;
    curio_alert_config_t config;
    void *states[CURIO_ALERT_MAX_RULES];
    curio_alert_notification_t queue[CURIO_ALERT_QUEUE];
    char keys[CURIO_ALERT_QUEUE][149];
    size_t count;
    uint32_t next_id, dropped;
    uint64_t shown_ms;
};
static uint64_t elapsed(uint64_t now, uint64_t then) { return now >= then ? now - then : 0; }
const curio_alert_type_t *curio_alert_type_find(const char *id)
{
    for (size_t i = 0; i < curio_alert_type_count; i++)
        if (!strcmp(id, curio_alert_types[i]->id)) return curio_alert_types[i];
    return NULL;
}

void curio_alert_config_free(curio_alert_config_t *c)
{
    if (!c) return;
    for (size_t i = 0; i < c->rule_count; i++) cJSON_free(c->rules[i].options);
    curio_alert_config_default(c);
}

bool curio_alert_config_clone(curio_alert_config_t *dst, const curio_alert_config_t *src)
{
    curio_alert_config_t copy = *src;
    for (size_t i = 0; i < copy.rule_count; i++) copy.rules[i].options = NULL;
    for (size_t i = 0; i < copy.rule_count; i++) {
        size_t size = src->rules[i].type->options_size;
        copy.rules[i].options = cJSON_malloc(size);
        if (!copy.rules[i].options) { curio_alert_config_free(&copy); return false; }
        memcpy(copy.rules[i].options, src->rules[i].options, size);
    }
    *dst = copy;
    return true;
}

bool curio_alert_rules_equal(const curio_alert_config_t *a, const curio_alert_config_t *b)
{
    if (a->rule_count != b->rule_count) return false;
    for (size_t i = 0; i < a->rule_count; i++) {
        const curio_alert_rule_t *x = &a->rules[i], *y = &b->rules[i];
        if (strcmp(x->id, y->id) || x->enabled != y->enabled || x->type != y->type ||
            x->display_s != y->display_s || x->sound != y->sound || x->vibrate != y->vibrate ||
            x->flash != y->flash || memcmp(x->options, y->options, x->type->options_size)) return false;
    }
    return true;
}

bool curio_alert_config_validate(const curio_alert_config_t *c)
{
    if (!c || c->rule_count > CURIO_ALERT_MAX_RULES || c->volume > 100 ||
        c->vibration_strength > 100) return false;
    for (size_t i = 0; i < c->rule_count; i++) {
        const curio_alert_rule_t *r = &c->rules[i];
        if (!curio_identifier(r->id, 48) || r->display_s > 300 || !r->type || !r->options ||
            curio_alert_type_find(r->type->id) != r->type || !r->type->validate(r->options)) return false;
        for (size_t j = 0; j < i; j++) if (!strcmp(r->id, c->rules[j].id)) return false;
    }
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

static void clear_states(curio_alerts_t *a)
{
    for (size_t i = 0; i < a->config.rule_count; i++)
        a->allocator.free(a->states[i]);
    memset(a->states, 0, sizeof(a->states));
}

void curio_alerts_destroy(curio_alerts_t *a)
{
    if (!a) return;
    clear_states(a);
    curio_alert_config_free(&a->config);
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
    if (!a || !curio_alert_config_validate(c)) return false;
    if (curio_alert_rules_equal(&a->config, c)) {
        curio_alerts_set_volume(a, c->volume);
        curio_alerts_set_vibration_strength(a, c->vibration_strength);
        a->config.flash_enabled = c->flash_enabled;
        a->config.revision = c->revision;
        return true;
    }
    curio_alert_config_t copy;
    if (!curio_alert_config_clone(&copy, c)) return false;
    void *states[CURIO_ALERT_MAX_RULES] = {0};
    for (size_t i = 0; i < c->rule_count; i++) {
        if (!c->rules[i].enabled) continue;
        size_t size = c->rules[i].type->state_size;
        states[i] = a->allocator.alloc(size);
        if (!states[i]) {
            for (size_t j = 0; j < i; j++) a->allocator.free(states[j]);
            curio_alert_config_free(&copy);
            return false;
        }
        memset(states[i], 0, size);
    }
    clear_states(a);
    curio_alert_config_free(&a->config);
    a->config = copy;
    memcpy(a->states, states, sizeof(states));
    a->count = 0;
    a->shown_ms = 0;
    return true;
}

static void remove_at(curio_alerts_t *a, size_t i)
{
    a->count--;
    memmove(a->queue+i, a->queue+i+1, (a->count-i)*sizeof(a->queue[0]));
    memmove(a->keys+i, a->keys+i+1, (a->count-i)*sizeof(a->keys[0]));
}

static void pop(curio_alerts_t *a, uint64_t now)
{
    if (!a->count) return;
    remove_at(a, 0);
    a->shown_ms = now;
}
typedef struct { curio_alerts_t *alerts; const curio_alert_rule_t *rule; uint64_t now; } delivery_t;
static void cancel(void *ctx, const char *key)
{
    delivery_t *d = ctx;
    for (size_t i = 1; i < d->alerts->count;)
        if (!strcmp(d->alerts->queue[i].rule_id, d->rule->id) &&
            !strcmp(d->alerts->keys[i], key)) remove_at(d->alerts, i);
        else i++;
}

static void emit(void *ctx, const char *key, const curio_alert_notification_t *notification)
{
    delivery_t *d = ctx;
    curio_alerts_t *a = d->alerts;
    const curio_alert_rule_t *r = d->rule;
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->queue[i].rule_id, r->id) && !strcmp(a->keys[i], key)) return;
    if (a->count == CURIO_ALERT_QUEUE) { a->dropped++; return; }
    curio_alert_notification_t n = *notification;
    n.id = ++a->next_id;
    strcpy(n.rule_id, r->id);
    n.sound = r->sound; n.vibrate = r->vibrate; n.flash = r->flash;
    n.display_s = r->display_s;
    n.volume = a->config.volume; n.vibration_strength = a->config.vibration_strength;
    size_t index = a->count;
    if (n.kind == CURIO_ALERT_WARNING)
        for (size_t i = a->count ? 1 : 0; i < a->count; i++)
            if (a->queue[i].kind == CURIO_ALERT_INFO) { index = i; break; }
    memmove(a->queue+index+1, a->queue+index, (a->count-index)*sizeof(a->queue[0]));
    memmove(a->keys+index+1, a->keys+index, (a->count-index)*sizeof(a->keys[0]));
    a->queue[index] = n;
    strncpy(a->keys[index], key, sizeof(a->keys[index])-1);
    a->keys[index][sizeof(a->keys[index])-1] = 0;
    if (!a->count++) a->shown_ms = d->now;
}

void curio_alerts_tick(curio_alerts_t *a, const curio_core_t *core, uint64_t now, bool wifi)
{
    if (!a || !core) return;
    if (a->count && a->queue[0].display_s &&
        elapsed(now, a->shown_ms) >= (uint64_t)a->queue[0].display_s*1000) pop(a, now);
    for (size_t i = 0; i < a->config.rule_count; i++) {
        if (!a->states[i]) continue;
        delivery_t d = {a, &a->config.rules[i], now};
        d.rule->type->evaluate(a->states[i], d.rule->options, core, now, wifi, emit, cancel, &d);
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
