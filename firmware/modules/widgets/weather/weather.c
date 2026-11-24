#include "weather.h"
#include "config.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

typedef struct location {
    struct location *next;
    double latitude, longitude;
    curio_weather_sample_t sample;
    uint64_t updated_at, next_refresh;
    bool retained;
} location_t;
static location_t *locations;

static location_t *find(double latitude, double longitude) {
    for (location_t *p = locations; p; p = p->next)
        if (p->latitude == latitude && p->longitude == longitude) return p;
    return NULL;
}

void curio_weather_configure(const curio_display_config_t *layout) {
    for (location_t *p = locations; p; p = p->next) p->retained = false;
    for (size_t i = 0; i < layout->page_count; i++) {
        const curio_page_config_t *page = &layout->pages[i];
        for (size_t j = 0; j < page->widget_count; j++) {
            const curio_widget_config_t *widget = &page->widgets[j];
            if (widget->type != &curio_widget_weather) continue;
            const curio_weather_config_t *config = widget->options;
            location_t *p = find(config->latitude, config->longitude);
            if (!p) {
                p = cJSON_malloc(sizeof(*p));
                if (!p) continue;
                memset(p, 0, sizeof(*p));
                p->latitude = config->latitude;
                p->longitude = config->longitude;
                p->next = locations;
                locations = p;
            }
            p->retained = true;
        }
    }
    location_t **link = &locations;
    while (*link) {
        location_t *p = *link;
        if (p->retained) link = &p->next;
        else { *link = p->next; cJSON_free(p); }
    }
}

bool curio_weather_contains(double latitude, double longitude) {
    return find(latitude, longitude) != NULL;
}

bool curio_weather_next(uint64_t now, double *latitude, double *longitude) {
    location_t *selected = NULL;
    for (location_t *p = locations; p; p = p->next)
        if (now >= p->next_refresh && (!selected || p->next_refresh < selected->next_refresh)) selected = p;
    if (!selected) return false;
    *latitude = selected->latitude;
    *longitude = selected->longitude;
    return true;
}

static bool number(const cJSON *object, const char *key, double min, double max, bool integer, double *value) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *value = item->valuedouble;
    return *value >= min && *value <= max && (!integer || floor(*value) == *value);
}

void curio_weather_complete(double latitude, double longitude, uint64_t now, const char *json) {
    location_t *p = find(latitude, longitude);
    if (!p) return;
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    double temperature, humidity, code;
    bool valid = cJSON_IsObject(current)
        && number(current, "temperature_2m", -100, 70, false, &temperature)
        && number(current, "relative_humidity_2m", 0, 100, true, &humidity)
        && number(current, "weather_code", 0, 99, true, &code);
    cJSON_Delete(root);
    p->sample.stale = !valid;
    p->next_refresh = now + (valid ? 900000 : 60000);
    if (valid) {
        p->sample.temperature = temperature;
        p->sample.humidity = (unsigned)humidity;
        p->sample.code = (unsigned)code;
        p->sample.valid = true;
        p->updated_at = now;
    }
}

curio_weather_sample_t curio_weather_read(double latitude, double longitude, uint64_t now) {
    location_t *p = find(latitude, longitude);
    if (!p) return (curio_weather_sample_t){0};
    curio_weather_sample_t sample = p->sample;
    sample.stale |= sample.valid && now - p->updated_at >= 1800000;
    return sample;
}

const char *curio_weather_description(unsigned code) {
    if (code == 0) return "Clear";
    if (code <= 2) return "Partly cloudy";
    if (code == 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code == 95 || code == 96 || code == 99) return "Thunderstorm";
    return "Unknown";
}
