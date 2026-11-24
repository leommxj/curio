#include "config.h"
#include "curio_module.h"
#include <stdio.h>
#include <string.h>

static bool validate(const void *options)
{
    const host_offline_options_t *r = options;
    if ((!curio_identifier(r->host_id, 48) && strcmp(r->host_id,"*"))) return false;
    return r->hold_s >= 5 && r->hold_s <= 86400;
}

static bool parse(const cJSON *json, void *options)
{
    host_offline_options_t *r = options;
    memset(r, 0, sizeof(*r));
    if (!curio_string(json,"host", r->host_id, sizeof(r->host_id),"*")) return false;
    double hold;
    if (!curio_number(json,"hold", 30, 5, 86400, true, &hold)) return false;
    r->hold_s = (uint32_t)hold;
    return validate(r);
}

static bool write(cJSON *json, const void *options)
{
    const host_offline_options_t *r = options;
    return (!strcmp(r->host_id, "*") || curio_put_string(json, "host", r->host_id)) && (r->hold_s == 30 ||
        curio_put_number(json,"hold", r->hold_s));
}

static void describe(const void *options, char *out, size_t size)
{
    const host_offline_options_t *r = options;
    snprintf(out, size, "Host: %s\nOffline for: %lus",
        strcmp(r->host_id, "*") ? r->host_id : "All hosts", (unsigned long)r->hold_s);
}

typedef struct {
    char host_id[49];
    bool seen_online, accumulating, alarmed;
    uint64_t started_ms;
} host_track_t;

typedef struct {
    host_track_t hosts[CURIO_MAX_HOSTS];
} rule_track_t;

static uint64_t elapsed(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

static void notify(const curio_host_t *h, curio_alert_emit_fn emit, void *ctx)
{
    curio_alert_notification_t n = {0};
    n.kind = CURIO_ALERT_WARNING;
    strcpy(n.host_id, h->host_id);

    strcpy(n.title, "Host offline");
    snprintf(n.body, sizeof(n.body), "%s", h->host_name);
    snprintf(n.detail, sizeof(n.detail), "%s", h->host_id);

    emit(ctx, h->host_id, &n);
}

static void tick_offline(host_track_t *t, const host_offline_options_t *r,
                         const curio_host_t *h, uint64_t now, bool wifi, curio_alert_emit_fn emit,
                         curio_alert_cancel_fn cancel, void *ctx)
{
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
        cancel(ctx, h->host_id);
    } else if (t->seen_online && !t->alarmed) {
        if (!t->accumulating) {
            t->accumulating = true;
            t->started_ms = now;
        } else if (elapsed(now, t->started_ms) >= (uint64_t)r->hold_s * 1000) {
            t->accumulating = false;
            t->alarmed = true;
            notify(h, emit, ctx);
        }
    }
}

static void evaluate(void *state, const void *options, const curio_core_t *core,
                     uint64_t now, bool wifi, curio_alert_emit_fn emit,
                     curio_alert_cancel_fn cancel, void *ctx)
{
    rule_track_t *rt = state;
    const host_offline_options_t *r = options;
    for (size_t j = 0; j < CURIO_MAX_HOSTS; j++) {
        const curio_host_t *h = &core->hosts[j];
        host_track_t *t = &rt->hosts[j];
        if (!h->occupied || strcmp(t->host_id, h->host_id)) {
            memset(t, 0, sizeof(*t));
            if (h->occupied) strcpy(t->host_id, h->host_id);
        }
        if (!h->occupied || (strcmp(r->host_id, "*") && strcmp(r->host_id, h->host_id))) continue;
        tick_offline(t, r, h, now, wifi, emit, cancel, ctx);
    }
}

const curio_alert_type_t curio_alert_host_offline = {
    .id = "host_offline",
    .title = "Host offline",
    .options_size = sizeof(host_offline_options_t),
    .fields = "host|hold",
    .parse = parse,
    .validate = validate,
    .write = write,
    .describe = describe,
    .state_size = sizeof(rule_track_t),
    .evaluate = evaluate
};
