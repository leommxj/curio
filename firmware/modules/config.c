#include "curio_module.h"
#include <math.h>
#include <string.h>

static bool listed(const char *key, const char *names)
{
    while (names && *names) {
        const char *end = strchr(names, '|');
        size_t size = end ? (size_t)(end - names) : strlen(names);
        if (strlen(key) == size && !memcmp(key, names, size)) return true;
        if (!end) break;
        names = end + 1;
    }
    return false;
}
bool curio_fields(const cJSON *object, const char *common, const char *specific)
{
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON *field = object->child; field; field = field->next)
        if (!listed(field->string, common) && !listed(field->string, specific)) return false;
    return true;
}
bool curio_string(const cJSON *o, const char *key, char *out, size_t size, const char *fallback)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    const char *s = v ? (cJSON_IsString(v) ? v->valuestring : NULL) : fallback;
    if (!s || strlen(s) >= size) return false;
    strcpy(out, s);
    return true;
}
bool curio_number(const cJSON *o, const char *key, double fallback, double min, double max, bool whole, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    double value = v ? v->valuedouble : fallback;
    if ((v && !cJSON_IsNumber(v)) || !isfinite(value) || value < min || value > max || (whole && floor(value) != value)) return false;
    *out = value;
    return true;
}
bool curio_boolean(const cJSON *o, const char *key, bool fallback, bool *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (v && !cJSON_IsBool(v)) return false;
    *out = v ? cJSON_IsTrue(v) : fallback;
    return true;
}
bool curio_identifier(const char *s, size_t max)
{
    size_t n = strlen(s);
    if (!n || n > max)
        return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              strchr("._-", *s)))
            return false;
    return true;
}
bool curio_unit(const char *s)
{
    if (strlen(s) > 16)
        return false;
    for (; *s; s++)
        if ((unsigned char)*s < 33 || (unsigned char)*s > 126)
            return false;
    return true;
}

bool curio_put_string(cJSON *o, const char *key, const char *value) { return cJSON_AddStringToObject(o, key, value) != NULL; }
bool curio_put_number(cJSON *o, const char *key, double value) { return cJSON_AddNumberToObject(o, key, value) != NULL; }
bool curio_put_bool(cJSON *o, const char *key, bool value) { return cJSON_AddBoolToObject(o, key, value) != NULL; }
