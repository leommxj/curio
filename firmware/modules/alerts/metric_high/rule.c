#include "config.h"
#include "curio_module.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool validate(const void *options)
{
    const metric_high_options_t *r = options;
    return curio_identifier(r->host_id, 48) && curio_identifier(r->source_id, 48) &&
        curio_identifier(r->key, 48) && r->unit[0] && curio_unit(r->unit) &&
        r->hold_s >= 1 && r->hold_s <= 86400 && isfinite(r->threshold) &&
        isfinite(r->recover) && r->recover < r->threshold;
}

static bool parse(const cJSON *json, void *options)
{
    metric_high_options_t *r = options;
    memset(r, 0, sizeof(*r));
    if (!curio_string(json,"host", r->host_id, sizeof(r->host_id), NULL)) return false;
    double hold;
    if (!curio_string(json,"source", r->source_id, sizeof(r->source_id), NULL) ||
        !curio_string(json,"key", r->key, sizeof(r->key), NULL) ||
        !curio_string(json,"unit", r->unit, sizeof(r->unit), NULL) ||
        !curio_number(json,"threshold", NAN, -DBL_MAX, DBL_MAX, false, &r->threshold) ||
        !curio_number(json,"recover", NAN, -DBL_MAX, DBL_MAX, false, &r->recover) ||
        !curio_number(json,"hold", 60, 1, 86400, true, &hold)) return false;
    r->hold_s = (uint32_t)hold;
    return validate(r);
}

static bool write(cJSON *json, const void *options)
{
    const metric_high_options_t *r = options;
    return curio_put_string(json,"host", r->host_id) && curio_put_string(json,"source", r->source_id) &&
        curio_put_string(json,"key", r->key) && curio_put_string(json,"unit", r->unit) &&
        curio_put_number(json,"threshold", r->threshold) && curio_put_number(json,"recover", r->recover) &&
        (r->hold_s == 60 || curio_put_number(json,"hold", r->hold_s));
}

static void describe(const void *options, char *out, size_t size)
{
    const metric_high_options_t *r = options;
    bool percent = !strcmp(r->unit, "percent");
    const char *unit = percent ? "%" : r->unit, *space = percent ? "" : " ";
    snprintf(out, size, "Host: %s\nMetric: %s / %s\nAbove: %.15g%s%s for %lus\nRecover: <= %.15g%s%s",
        r->host_id, r->source_id, r->key, r->threshold, space, unit,
        (unsigned long)r->hold_s, r->recover, space, unit);
}

typedef struct {
    bool accumulating, alarmed;
    uint64_t started_ms;
} rule_track_t;

static uint64_t elapsed(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

static void notify(const curio_host_t *h, double value, const metric_high_options_t *r, curio_alert_emit_fn emit, void *ctx)
{
    curio_alert_notification_t n = {0};
    n.kind = CURIO_ALERT_WARNING;
    strcpy(n.host_id, h->host_id);

    strcpy(n.title, "Metric high");
    snprintf(n.body, sizeof(n.body), "%s", h->host_name);
    char formatted[64];
    curio_format_metric(formatted, sizeof(formatted), value, r->unit);
    snprintf(n.detail, sizeof(n.detail), "%s/%s: %.29s", r->source_id, r->key, formatted);

    emit(ctx, h->host_id, &n);
}

static void tick_metric(rule_track_t *t, const metric_high_options_t *r,
                        const curio_host_t *h, uint64_t now, curio_alert_emit_fn emit, curio_alert_cancel_fn cancel, void *ctx)
{
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
        cancel(ctx, h->host_id);
    } else if (m->value > r->threshold && !t->alarmed) {
        if (!t->accumulating) {
            t->accumulating = true;
            t->started_ms = now;
        } else if (elapsed(now, t->started_ms) >= (uint64_t)r->hold_s * 1000) {
            t->alarmed = true;
            t->accumulating = false;
            notify(h, m->value, r, emit, ctx);
        }
    } else
        t->accumulating = false;
}

static void evaluate(void *state, const void *options, const curio_core_t *core,
                     uint64_t now, bool wifi, curio_alert_emit_fn emit,
                     curio_alert_cancel_fn cancel, void *ctx)
{
    (void)wifi;
    rule_track_t *rt = state;
    const metric_high_options_t *r = options;
    const curio_host_t *host = curio_core_host(core, r->host_id);
    if (!host) {
        memset(rt, 0, sizeof(*rt));
        return;
    }
    tick_metric(rt, r, host, now, emit, cancel, ctx);
}

const curio_alert_type_t curio_alert_metric_high = {
    .id = "metric_high",
    .title = "Metric high",
    .options_size = sizeof(metric_high_options_t),
    .fields = "host|source|key|unit|threshold|recover|hold",
    .parse = parse,
    .validate = validate,
    .write = write,
    .describe = describe,
    .state_size = sizeof(rule_track_t),
    .evaluate = evaluate
};
