#include "config.h"
#include "curio_module.h"
#include <math.h>
#include <string.h>

static bool validate(const void *options)
{
    const curio_weather_config_t *c = options;
    return c->city[0] && memchr(c->city, 0, sizeof(c->city)) &&
        isfinite(c->latitude) && c->latitude >= -90 && c->latitude <= 90 &&
        isfinite(c->longitude) && c->longitude >= -180 && c->longitude <= 180;
}
static bool parse(const cJSON *o, void *options)
{
    curio_weather_config_t *c = options;
    memset(c, 0, sizeof(*c));
    return curio_string(o, "city", c->city, sizeof(c->city), NULL) &&
        curio_number(o, "latitude", NAN, -90, 90, false, &c->latitude) &&
        curio_number(o, "longitude", NAN, -180, 180, false, &c->longitude) && validate(c);
}
static bool write(cJSON *o, const void *options)
{
    const curio_weather_config_t *c = options;
    return curio_put_string(o, "city", c->city) && curio_put_number(o, "latitude", c->latitude) &&
        curio_put_number(o, "longitude", c->longitude);
}
const curio_widget_type_t curio_widget_weather = {
    .id = "weather", .title = "", .fields = "city|latitude|longitude",
    .min_cols = 1, .min_rows = 1, .services = CURIO_SERVICE_TIME,
    .options_size = sizeof(curio_weather_config_t), .parse = parse, .validate = validate, .write = write,
};
