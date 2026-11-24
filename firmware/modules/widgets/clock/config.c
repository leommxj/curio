#include "config.h"
#include "curio_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool validate(const void *options)
{
    const curio_clock_config_t *c = options;
    return c->utc_offset_minutes >= -720 && c->utc_offset_minutes <= 840;
}
static bool parse(const cJSON *o, void *options)
{
    curio_clock_config_t *c = options;
    memset(c, 0, sizeof(*c));
    char s[7];
    if (!curio_string(o, "utc_offset", s, sizeof(s), "+00:00") || strlen(s) != 6 ||
        (s[0] != '+' && s[0] != '-') || s[3] != ':' ||
        s[1] < '0' || s[1] > '9' || s[2] < '0' || s[2] > '9' ||
        s[4] < '0' || s[4] > '5' || s[5] < '0' || s[5] > '9') return false;
    int minutes = ((s[1] - '0') * 10 + s[2] - '0') * 60 + (s[4] - '0') * 10 + s[5] - '0';
    c->utc_offset_minutes = s[0] == '-' ? -minutes : minutes;
    return curio_boolean(o, "seconds", false, &c->seconds) && validate(c);
}
static bool write(cJSON *o, const void *options)
{
    const curio_clock_config_t *c = options;
    int offset = abs(c->utc_offset_minutes);
    char zone[16];
    snprintf(zone, sizeof(zone), "%c%02d:%02d", c->utc_offset_minutes < 0 ? '-' : '+', offset / 60, offset % 60);
    return (!offset || curio_put_string(o, "utc_offset", zone)) && (!c->seconds || curio_put_bool(o, "seconds", true));
}
const curio_widget_type_t curio_widget_clock = {
    .id = "clock", .title = "", .fields = "utc_offset|seconds",
    .min_cols = 1, .min_rows = 1, .services = CURIO_SERVICE_TIME,
    .options_size = sizeof(curio_clock_config_t), .parse = parse, .validate = validate, .write = write,
};
