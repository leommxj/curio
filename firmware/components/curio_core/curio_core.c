#include "curio_core.h"

#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint64_t elapsed(uint64_t now, uint64_t then)
{
    return now >= then ? now - then : 0;
}

bool curio_utf8_valid(const unsigned char *s, size_t len)
{
    for (size_t i = 0; i < len;) {
        unsigned char c = s[i++];
        if (c < 0x80) {
            if (c == 0)
                return false;
            continue;
        }
        unsigned count;
        uint32_t value;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            count = 1;
            value = c & 0x1f;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            count = 2;
            value = c & 0x0f;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3;
            value = c & 0x07;
            minimum = 0x10000;
        } else
            return false;
        if (count > len - i)
            return false;
        for (unsigned j = 0; j < count; ++j) {
            c = s[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (c & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

static bool digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool hex4(const char *s, uint32_t *value)
{
    *value = 0;
    for (size_t i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)s[i];
        unsigned n;
        if (c >= '0' && c <= '9')
            n = c - '0';
        else if (c >= 'a' && c <= 'f')
            n = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            n = c - 'A' + 10;
        else
            return false;
        *value = (*value << 4) | n;
    }
    return true;
}

static bool json_number(const char *s, size_t length, size_t *position)
{
    size_t i = *position;
    if (s[i] == '-')
        ++i;
    if (i == length)
        return false;
    if (s[i] == '0')
        ++i;
    else {
        if (s[i] < '1' || s[i] > '9')
            return false;
        while (i < length && digit(s[i]))
            ++i;
    }
    if (i < length && s[i] == '.') {
        ++i;
        if (i == length || !digit(s[i]))
            return false;
        while (i < length && digit(s[i]))
            ++i;
    }
    if (i < length && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < length && (s[i] == '+' || s[i] == '-'))
            ++i;
        if (i == length || !digit(s[i]))
            return false;
        while (i < length && digit(s[i]))
            ++i;
    }
    if (i < length && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n' && s[i] != ',' &&
        s[i] != ']' && s[i] != '}')
        return false;
    *position = i - 1;
    return true;
}

/* Bound parser recursion and enforce JSON lexical rules cJSON relaxes.
 * Escaped NUL cannot be represented by the public C string fields. */
static bool json_preflight(const char *s, size_t length)
{
    bool in_string = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            if (c < 0x20)
                return false;
            if (c != '\\')
                continue;
            if (++i == length)
                return false;
            if (s[i] == 'u') {
                uint32_t code;
                if (length - i <= 4 || !hex4(s + i + 1, &code) || !code ||
                    (code >= 0xdc00 && code <= 0xdfff))
                    return false;
                i += 4;
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (length - i <= 6 || s[i + 1] != '\\' || s[i + 2] != 'u' ||
                        !hex4(s + i + 3, &code) || code < 0xdc00 || code > 0xdfff)
                        return false;
                    i += 6;
                }
            } else if (!strchr("\"\\/bfnrt", s[i]))
                return false;
        } else {
            if (c < 0x20 && c != '\t' && c != '\r' && c != '\n')
                return false;
            if (c == '-' || digit((char)c)) {
                if (!json_number(s, length, &i))
                    return false;
                continue;
            }
            if (c == '{' || c == '[') {
                if (++depth > 32)
                    return false;
            } else if (c == '}' || c == ']') {
                if (!depth)
                    return false;
                --depth;
            }
        }
    }
    return !in_string && !depth;
}

bool curio_json_valid(const char *s, size_t n)
{
    return s && n && curio_utf8_valid((const unsigned char *)s, n) && json_preflight(s, n);
}
static bool unique_fields(const cJSON *o)
{
    const cJSON *a, *b;
    cJSON_ArrayForEach(a, o)
    {
        if (cJSON_IsObject(o))
            for (b = a->next; b; b = b->next)
                if (a->string && b->string && !strcmp(a->string, b->string))
                    return false;
        if ((cJSON_IsObject(a) || cJSON_IsArray(a)) && !unique_fields(a))
            return false;
    }
    return true;
}
static const cJSON *field(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static bool copy_string(const cJSON *item, char *dest, size_t min, size_t max)
{
    if (!cJSON_IsString(item) || !item->valuestring)
        return false;
    size_t len = strlen(item->valuestring);
    if (len < min || len > max || !curio_utf8_valid((const unsigned char *)item->valuestring, len))
        return false;
    memcpy(dest, item->valuestring, len + 1);
    return true;
}

static bool host_id_valid(const char *id)
{
    for (; *id; ++id) {
        unsigned char c = (unsigned char)*id;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return false;
    }
    return true;
}

const char *curio_state_name(curio_agent_state_t state)
{
    static const char *const names[] = {"blocked", "working", "done", "idle", "unknown"};
    return state <= CURIO_STATE_UNKNOWN && state >= CURIO_STATE_BLOCKED ? names[state] : "unknown";
}

const char *curio_health_name(curio_health_t health)
{
    static const char *const names[] = {"fresh", "stale", "unavailable", "offline"};
    return health <= CURIO_HEALTH_OFFLINE && health >= CURIO_HEALTH_FRESH ? names[health]
                                                                          : "offline";
}

static bool parse_state(const cJSON *item, curio_agent_state_t *state)
{
    if (!cJSON_IsString(item))
        return false;
    for (int i = CURIO_STATE_BLOCKED; i <= CURIO_STATE_UNKNOWN; ++i) {
        if (strcmp(item->valuestring, curio_state_name((curio_agent_state_t)i)) == 0) {
            *state = (curio_agent_state_t)i;
            return true;
        }
    }
    return false;
}

struct curio_snapshot {
    curio_host_t host;
};
static void *allocate(const curio_core_t *c, size_t n)
{
    void *p = c->allocator.alloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}
static void release_host(const curio_core_t *c, curio_host_t *h)
{
    for (size_t i = 0; i < h->source_count; i++) {
        c->allocator.free(h->sources[i].metrics);
        c->allocator.free(h->sources[i].agents);
        c->allocator.free(h->sources[i].state);
    }
    c->allocator.free(h->sources);
    memset(h, 0, sizeof(*h));
}
void curio_core_init_allocator(curio_core_t *c, curio_allocator_t a)
{
    memset(c, 0, sizeof(*c));
    c->allocator = a;
}
void curio_core_init(curio_core_t *c)
{
    curio_core_init_allocator(c, (curio_allocator_t){malloc, free});
}
void curio_core_destroy(curio_core_t *c)
{
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++)
        release_host(c, &c->hosts[i]);
}
void curio_core_snapshot_free(const curio_core_t *c, curio_snapshot_t *s)
{
    if (s) {
        release_host(c, &s->host);
        c->allocator.free(s);
    }
}
const curio_host_t *curio_core_host(const curio_core_t *c, const char *id)
{
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++)
        if (c->hosts[i].occupied && !strcmp(c->hosts[i].host_id, id))
            return &c->hosts[i];
    return NULL;
}
const curio_source_t *curio_host_source(const curio_host_t *h, const char *id)
{
    if (h)
        for (size_t i = 0; i < h->source_count; i++)
            if (!strcmp(h->sources[i].id, id))
                return &h->sources[i];
    return NULL;
}
const curio_metric_t *curio_source_metric(const curio_source_t *s, const char *key)
{
    if (s)
        for (size_t i = 0; i < s->metric_count; i++)
            if (!strcmp(s->metrics[i].key, key))
                return &s->metrics[i];
    return NULL;
}
const char *curio_source_status_name(curio_source_status_t s)
{
    static const char *names[] = {"ok", "error", "unavailable", "warming_up"};
    return s <= CURIO_SOURCE_WARMING_UP ? names[s] : "error";
}
static bool integer(const cJSON *v)
{
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble >= 0 &&
           v->valuedouble <= 9007199254740991.0 && floor(v->valuedouble) == v->valuedouble;
}
static bool unit_valid(const char *u)
{
    for (; *u; u++)
        if ((unsigned char)*u < 33 || (unsigned char)*u > 126)
            return false;
    return true;
}
static bool metric_valid(const curio_metric_t *m)
{
    return isfinite(m->value) &&
           (!strcmp(m->unit, "percent") ? (m->value >= 0 && m->value <= 100)
            : !strcmp(m->unit, "bytes_per_second") ? m->value >= 0
            : (!strcmp(m->unit, "bytes") || !strcmp(m->unit, "count"))
                ? (m->value >= 0 && m->value <= 9007199254740991.0 && floor(m->value) == m->value)
                : true);
}
static bool parse_agents(const cJSON *a, curio_source_t *s)
{
    for (size_t i = 0; i < s->agent_count; i++) {
        curio_agent_t *v = &s->agents[i];
        const cJSON *o = cJSON_GetArrayItem(a, (int)i);
        if (!cJSON_IsObject(o) || !copy_string(field(o, "id"), v->id, 1, 96) ||
            !copy_string(field(o, "name"), v->name, 1, 64) ||
            !copy_string(field(o, "project"), v->project, 0, 48) ||
            !parse_state(field(o, "state"), &v->state))
            return false;
        const cJSON *brand = field(o, "brand");
        if (brand && !copy_string(brand, v->brand, 0, 32))
            return false;
        for (size_t j = 0; j < i; j++)
            if (!strcmp(v->id, s->agents[j].id))
                return false;
    }
    return true;
}
curio_result_t curio_core_parse(const curio_core_t *c, const char *body, size_t length,
                                curio_snapshot_t **out)
{
    *out = NULL;
    if (length > CURIO_MAX_BODY_BYTES)
        return CURIO_TOO_LARGE;
    if (!curio_json_valid(body, length))
        return CURIO_INVALID;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length, &end, 0);
    if (!root)
        return CURIO_INVALID;
    if (!unique_fields(root)) {
        cJSON_Delete(root);
        return CURIO_INVALID;
    }
    while (end < body + length && strchr(" \r\n\t", *end))
        end++;
    if (end != body + length) {
        cJSON_Delete(root);
        return CURIO_INVALID;
    }
    curio_snapshot_t *snap = allocate(c, sizeof(*snap));
    curio_result_t result = CURIO_INVALID;
    if (!snap) {
        cJSON_Delete(root);
        return CURIO_NO_MEMORY;
    }
    curio_host_t *h = &snap->host;
    const cJSON *v = field(root, "v"), *sources = field(root, "sources");
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(v) ||
        v->valuedouble != 2 ||
        !copy_string(field(root, "host_id"), h->host_id, 1, 48) || !host_id_valid(h->host_id) ||
        !copy_string(field(root, "host_name"), h->host_name, 1, 48))
        goto done;
    if (!cJSON_IsObject(sources)) goto done;
    size_t source_count = (size_t)cJSON_GetArraySize(sources);
    if (source_count > CURIO_MAX_SOURCES) goto done;
    if (source_count) {
        h->sources = allocate(c, source_count * sizeof(*h->sources));
        if (!h->sources) {
            result = CURIO_NO_MEMORY;
            goto done;
        }
    }
    h->source_count = source_count;
    size_t metrics = 0, agents = 0;
    for (size_t i = 0; i < h->source_count; i++) {
        curio_source_t *s = &h->sources[i];
        const cJSON *o = cJSON_GetArrayItem(sources, (int)i);
        if (!o->string || strlen(o->string) > 48 || !*o->string || !host_id_valid(o->string) ||
            !cJSON_IsObject(o))
            goto done;
        strcpy(s->id, o->string);
        const cJSON *kind = field(o, "kind"), *status = field(o, "status"),
                    *age = field(o, "sample_age_ms");
        if (!cJSON_IsString(kind) || !cJSON_IsString(status) || !integer(age))
            goto done;
        if (!strcmp(kind->valuestring, "metrics"))
            s->kind = CURIO_SOURCE_METRICS;
        else if (!strcmp(kind->valuestring, "agents"))
            s->kind = CURIO_SOURCE_AGENTS;
        else if (!strcmp(kind->valuestring, "status"))
            s->kind = CURIO_SOURCE_STATUS;
        else
            goto done;
        bool found = false;
        for (int k = 0; k <= CURIO_SOURCE_WARMING_UP; k++)
            if (!strcmp(status->valuestring,
                        curio_source_status_name((curio_source_status_t)k))) {
                s->status = (curio_source_status_t)k;
                found = true;
            }
        if (!found)
            goto done;
        const cJSON *max_age = field(o, "max_age_ms");
        s->max_age_ms = CURIO_STALE_MS;
        if (max_age) {
            if (!integer(max_age) || max_age->valuedouble < 1 || max_age->valuedouble > CURIO_MAX_AGE_MS)
                goto done;
            s->max_age_ms = (uint64_t)max_age->valuedouble;
        }
        s->sample_age_ms = (uint64_t)age->valuedouble;
        const cJSON *state = field(o, "state");
        if (s->kind != CURIO_SOURCE_STATUS && state)
            goto done;
        if (s->kind == CURIO_SOURCE_STATUS) {
            if (s->status != CURIO_SOURCE_OK) {
                if (state) goto done;
                continue;
            }
            if (!cJSON_IsObject(state)) goto done;
            s->state = allocate(c, sizeof(*s->state));
            if (!s->state) { result = CURIO_NO_MEMORY; goto done; }
            if (!copy_string(field(state, "text"), s->state->text, 1, 64)) goto done;
            const cJSON *detail = field(state, "detail"), *level = field(state, "level");
            if (detail && !copy_string(detail, s->state->detail, 0, 160)) goto done;
            if (level) {
                if (!cJSON_IsString(level)) goto done;
                bool valid = false;
                for (int k = CURIO_STATUS_NEUTRAL; k <= CURIO_STATUS_ERROR; k++)
                    if (!strcmp(level->valuestring, curio_status_level_name((curio_status_level_t)k))) {
                        s->state->level = (curio_status_level_t)k;
                        valid = true;
                    }
                if (!valid) goto done;
            }
            continue;
        }
        if (s->kind == CURIO_SOURCE_AGENTS) {
            const cJSON *a = field(o, "agents");
            if (!cJSON_IsArray(a))
                goto done;
            s->agent_count = (size_t)cJSON_GetArraySize(a);
            agents += s->agent_count;
            if (agents > CURIO_MAX_AGENTS || (s->status != CURIO_SOURCE_OK && s->agent_count))
                goto done;
            if (s->agent_count) {
                s->agents = allocate(c, s->agent_count * sizeof(*s->agents));
                if (!s->agents) {
                    result = CURIO_NO_MEMORY;
                    goto done;
                }
            }
            if (!parse_agents(a, s))
                goto done;
        } else {
            const cJSON *m = field(o, "metrics");
            if (!cJSON_IsObject(m))
                goto done;
            s->metric_count = (size_t)cJSON_GetArraySize(m);
            metrics += s->metric_count;
            if (metrics > CURIO_MAX_METRICS)
                goto done;
            if (s->metric_count) {
                s->metrics = allocate(c, s->metric_count * sizeof(*s->metrics));
                if (!s->metrics) {
                    result = CURIO_NO_MEMORY;
                    goto done;
                }
            }
            for (size_t j = 0; j < s->metric_count; j++) {
                const cJSON *entry = cJSON_GetArrayItem(m, (int)j);
                curio_metric_t *x = &s->metrics[j];
                if (!entry->string || !*entry->string || strlen(entry->string) > 48 ||
                    !host_id_valid(entry->string) || !cJSON_IsObject(entry) ||
                    !copy_string(field(entry, "unit"), x->unit, 1, 16) || !unit_valid(x->unit))
                    goto done;
                strcpy(x->key, entry->string);
                const cJSON *value = field(entry, "value");
                x->has_value = value != NULL;
                if (s->status == CURIO_SOURCE_OK) {
                    if (!cJSON_IsNumber(value))
                        goto done;
                    x->value = value->valuedouble;
                    if (!metric_valid(x))
                        goto done;
                } else if (value)
                    goto done;
            }
        }
    }
    result = CURIO_OK;
    *out = snap;
done:
    cJSON_Delete(root);
    if (result != CURIO_OK)
        curio_core_snapshot_free(c, snap);
    return result;
}
uint64_t curio_metric_age(const curio_metric_t *m, uint64_t now)
{
    uint64_t d = elapsed(now, m->received_ms);
    return UINT64_MAX - m->value_age_ms < d ? UINT64_MAX : m->value_age_ms + d;
}
curio_health_t curio_host_health(const curio_host_t *h, uint64_t now)
{
    if (!h || !h->occupied || elapsed(now, h->last_report_ms) > CURIO_OFFLINE_MS)
        return CURIO_HEALTH_OFFLINE;
    return elapsed(now, h->last_report_ms) > CURIO_STALE_MS ? CURIO_HEALTH_STALE : CURIO_HEALTH_FRESH;
}
curio_health_t curio_source_health(const curio_source_t *s, uint64_t now)
{
    if (!s || s->status != CURIO_SOURCE_OK || !s->has_success)
        return CURIO_HEALTH_UNAVAILABLE;
    return s->sample_age_ms > s->max_age_ms ||
                   elapsed(now, s->received_ms) > s->max_age_ms - s->sample_age_ms
               ? CURIO_HEALTH_STALE : CURIO_HEALTH_FRESH;
}
const char *curio_status_level_name(curio_status_level_t level)
{
    static const char *const names[] = {"neutral", "ok", "warning", "error"};
    return level >= CURIO_STATUS_NEUTRAL && level <= CURIO_STATUS_ERROR ? names[level] : "neutral";
}
curio_result_t curio_core_apply(curio_core_t *c, curio_snapshot_t *snap, uint64_t now)
{
    curio_host_t *slot = NULL, *h = &snap->host;
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++)
        if (c->hosts[i].occupied && !strcmp(c->hosts[i].host_id, h->host_id)) {
            slot = &c->hosts[i];
            break;
        }
    if (!slot)
        for (size_t i = 0; i < CURIO_MAX_HOSTS; i++)
            if (!c->hosts[i].occupied) {
                slot = &c->hosts[i];
                break;
            }
    if (!slot)
        for (size_t i = 0; i < CURIO_MAX_HOSTS; i++)
            if (elapsed(now, c->hosts[i].last_report_ms) >
                    CURIO_OFFLINE_MS + CURIO_EVICT_OFFLINE_MS &&
                (!slot || c->hosts[i].last_report_ms < slot->last_report_ms))
                slot = &c->hosts[i];
    if (!slot)
        return CURIO_CAPACITY;
    bool same = slot->occupied && !strcmp(slot->host_id, h->host_id);
    size_t retained_agents = 0;
    for (size_t i = 0; i < h->source_count; i++) {
        curio_source_t *s = &h->sources[i];
        const curio_source_t *old = same ? curio_host_source(slot, s->id) : NULL;
        s->received_ms = now;
        s->has_success = s->status == CURIO_SOURCE_OK;
        if (s->status != CURIO_SOURCE_OK && old && s->kind == old->kind) {
            s->has_success = old->has_success;
            uint64_t age = elapsed(now, old->received_ms);
            s->sample_age_ms = UINT64_MAX - old->sample_age_ms < age ? UINT64_MAX : old->sample_age_ms + age;
            if (s->kind == CURIO_SOURCE_AGENTS)
                retained_agents += old->agent_count;
        } else
            retained_agents += s->agent_count;
    }
    if (retained_agents > CURIO_MAX_AGENTS) return CURIO_CAPACITY;
    for (size_t i = 0; i < h->source_count; i++) {
        curio_source_t *s = &h->sources[i];
        curio_source_t *old = same ? (curio_source_t *)curio_host_source(slot, s->id) : NULL;
        if (s->kind == CURIO_SOURCE_METRICS)
            for (size_t j = 0; j < s->metric_count; j++) {
                curio_metric_t *m = &s->metrics[j];
                m->received_ms = now;
                m->value_age_ms = s->sample_age_ms;
                const curio_metric_t *prev =
                    old && old->kind == s->kind ? curio_source_metric(old, m->key) : NULL;
                if (!m->has_value && prev && prev->has_value && !strcmp(m->unit, prev->unit)) {
                    m->value = prev->value;
                    m->has_value = true;
                    m->value_age_ms = curio_metric_age(prev, now);
                }
            }
        if (s->status != CURIO_SOURCE_OK && old && old->kind == s->kind &&
            s->kind == CURIO_SOURCE_STATUS) {
            s->state = old->state;
            old->state = NULL;
        }
        if (s->status != CURIO_SOURCE_OK && old && old->kind == s->kind &&
            s->kind == CURIO_SOURCE_AGENTS) {
            s->agents = old->agents;
            s->agent_count = old->agent_count;
            old->agents = NULL;
        }
    }
    release_host(c, slot);
    h->occupied = true;
    h->last_report_ms = now;
    *slot = *h;
    memset(h, 0, sizeof(*h));
    return CURIO_OK;
}
curio_result_t curio_core_apply_json(curio_core_t *c, const char *body, size_t n, uint64_t now)
{
    curio_snapshot_t *s = NULL;
    curio_result_t r = curio_core_parse(c, body, n, &s);
    if (r == CURIO_OK)
        r = curio_core_apply(c, s, now);
    curio_core_snapshot_free(c, s);
    return r;
}

void curio_core_free_json(char *json)
{
    cJSON_free(json);
}

static cJSON *json_checked(cJSON *item, bool *failed)
{
    if (!item)
        *failed = true;
    return item;
}
static bool json_attach(cJSON *array, cJSON *item, bool *failed)
{
    if (!item || !cJSON_AddItemToArray(array, item)) {
        cJSON_Delete(item);
        *failed = true;
        return false;
    }
    return true;
}
static char *v2_json(const curio_core_t *core, uint64_t now, bool catalog)
{
    bool failed = false;
    cJSON *root = json_checked(cJSON_CreateObject(), &failed);
    if (!root)
        return NULL;
    json_checked(cJSON_AddNumberToObject(root, "v", 2), &failed);
    cJSON *hosts = json_checked(cJSON_AddArrayToObject(root, "hosts"), &failed);
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++) {
        const curio_host_t *h = &core->hosts[i];
        if (!h->occupied)
            continue;
        cJSON *o = json_checked(cJSON_CreateObject(), &failed);
        if (!json_attach(hosts, o, &failed))
            goto fail;
        json_checked(cJSON_AddStringToObject(o, "host_id", h->host_id), &failed);
        json_checked(cJSON_AddStringToObject(o, "host_name", h->host_name), &failed);
        json_checked(cJSON_AddStringToObject(
                         o, "health",
                         curio_health_name(curio_host_health(h, now))),
                     &failed);
        cJSON *sources = json_checked(cJSON_AddObjectToObject(o, "sources"), &failed);
        for (size_t j = 0; j < h->source_count; j++) {
            const curio_source_t *s = &h->sources[j];
            cJSON *x = json_checked(cJSON_AddObjectToObject(sources, s->id), &failed);
            json_checked(cJSON_AddStringToObject(
                             x, "kind", s->kind == CURIO_SOURCE_METRICS ? "metrics" : s->kind == CURIO_SOURCE_AGENTS ? "agents" : "status"),
                         &failed);
            json_checked(cJSON_AddStringToObject(x, "status", curio_source_status_name(s->status)),
                         &failed);
            json_checked(cJSON_AddStringToObject(x, "health",
                                                 curio_health_name(curio_source_health(s, now))),
                         &failed);
            json_checked(cJSON_AddNumberToObject(x, "max_age_ms", (double)s->max_age_ms), &failed);
            json_checked(
                cJSON_AddNumberToObject(x, "sample_age_ms",
                                        (double)(s->sample_age_ms + elapsed(now, s->received_ms))),
                &failed);
            if (s->kind == CURIO_SOURCE_METRICS) {
                cJSON *metrics = json_checked(cJSON_AddObjectToObject(x, "metrics"), &failed);
                for (size_t k = 0; k < s->metric_count; k++) {
                    const curio_metric_t *m = &s->metrics[k];
                    cJSON *v = json_checked(cJSON_AddObjectToObject(metrics, m->key), &failed);
                    json_checked(cJSON_AddStringToObject(v, "unit", m->unit), &failed);
                    if (!catalog && m->has_value && metric_valid(m)) {
                        json_checked(cJSON_AddNumberToObject(v, "value", m->value), &failed);
                        json_checked(
                            cJSON_AddNumberToObject(v, "age_ms", (double)curio_metric_age(m, now)),
                            &failed);
                    }
                }
            } else if (!catalog && s->kind == CURIO_SOURCE_STATUS && s->state) {
                cJSON *state = json_checked(cJSON_AddObjectToObject(x, "state"), &failed);
                json_checked(cJSON_AddStringToObject(state, "text", s->state->text), &failed);
                json_checked(cJSON_AddStringToObject(state, "level", curio_status_level_name(s->state->level)), &failed);
                if (s->state->detail[0])
                    json_checked(cJSON_AddStringToObject(state, "detail", s->state->detail), &failed);
            } else if (!catalog && s->kind == CURIO_SOURCE_AGENTS) {
                cJSON *agents = json_checked(cJSON_AddArrayToObject(x, "agents"), &failed);
                for (size_t k = 0; k < s->agent_count; k++) {
                    const curio_agent_t *a = &s->agents[k];
                    cJSON *v = json_checked(cJSON_CreateObject(), &failed);
                    if (!json_attach(agents, v, &failed))
                        goto fail;
                    json_checked(cJSON_AddStringToObject(v, "id", a->id), &failed);
                    json_checked(cJSON_AddStringToObject(v, "name", a->name), &failed);
                    json_checked(cJSON_AddStringToObject(v, "project", a->project), &failed);
                    if (a->brand[0])
                        json_checked(cJSON_AddStringToObject(v, "brand", a->brand), &failed);
                    json_checked(cJSON_AddStringToObject(v, "state", curio_state_name(a->state)),
                                 &failed);
                }
            }
        }
    }
fail:;
    char *json = failed ? NULL : cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
char *curio_core_state_v2_json(const curio_core_t *c, uint64_t now)
{
    return v2_json(c, now, false);
}
char *curio_core_catalog_json(const curio_core_t *c, uint64_t now)
{
    return v2_json(c, now, true);
}

void curio_format_metric(char *out, size_t size, double value, const char *unit)
{
    if (!size) return;
    if (!isfinite(value)) { snprintf(out, size, "--"); return; }
    if (!unit) unit = "";
    const char *suffix = unit;
    bool integer_value = false;
    bool percent = false;
    if (!strcmp(unit, "bytes") || !strcmp(unit, "bytes_per_second")) {
        static const char *const bytes[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
        static const char *const rates[] = {"B/s", "KiB/s", "MiB/s", "GiB/s", "TiB/s", "PiB/s"};
        unsigned i = 0;
        while (fabs(value) >= 1024 && i < 5) { value /= 1024; i++; }
        suffix = !strcmp(unit, "bytes") ? bytes[i] : rates[i];
    } else if (!strcmp(unit, "watts") || !strcmp(unit, "hertz")) {
        static const char *const watts[] = {"W", "kW", "MW"};
        static const char *const hertz[] = {"Hz", "kHz", "MHz", "GHz"};
        bool hz = !strcmp(unit, "hertz");
        unsigned i = 0;
        while (fabs(value) >= 1000 && i < (hz ? 3u : 2u)) { value /= 1000; i++; }
        suffix = hz ? hertz[i] : watts[i];
    } else if (!strcmp(unit, "seconds") || !strcmp(unit, "milliseconds")) {
        if (!strcmp(unit, "milliseconds")) value /= 1000;
        if (fabs(value) < 1 && value != 0) { value *= 1000; suffix = "ms"; }
        else if (fabs(value) < 60) suffix = "s";
        else if (fabs(value) < 3600) { value /= 60; suffix = "min"; }
        else if (fabs(value) < 86400) { value /= 3600; suffix = "h"; }
        else { value /= 86400; suffix = "d"; }
    } else if (!strcmp(unit, "celsius")) suffix = "C";
    else if (!strcmp(unit, "percent")) { suffix = "%"; percent = true; }
    else if (!strcmp(unit, "count")) { suffix = ""; integer_value = true; }
    else if (!strcmp(unit, "number")) suffix = "";
    char number[384];
    if (fabs(value) < 1e14) value = integer_value ? round(value) : round(value * 10) / 10;
    if (fabs(value) < (integer_value ? 0.5 : 0.05)) value = 0;
    snprintf(number, sizeof(number), integer_value ? "%.0f" : "%.1f", value);
    size_t n = strlen(number);
    if (!integer_value && n >= 2 && !strcmp(number + n - 2, ".0")) number[n - 2] = 0;
    snprintf(out, size, "%s%s%s", number, *suffix && !percent ? " " : "", suffix);
}
