#include "config.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <string.h>

void test_widget_weather(void)
{
    curio_weather_config_t c = {.city = "北京", .latitude = 39.9, .longitude = 116.4};
    assert(curio_widget_weather.validate(&c));
    c.latitude = NAN;
    assert(!curio_widget_weather.validate(&c));
    c.latitude = 0;
    c.longitude = INFINITY;
    assert(!curio_widget_weather.validate(&c));
    c.longitude = 0;
    memset(c.city, 'a', sizeof(c.city));
    assert(!curio_widget_weather.validate(&c));
    c.city[64] = 0;
    assert(curio_widget_weather.validate(&c));
    cJSON *o = cJSON_CreateObject();
    assert(o && curio_widget_weather.write(o, &c));
    curio_weather_config_t copy;
    assert(curio_widget_weather.parse(o, &copy));
    assert(!strcmp(c.city, copy.city) && c.latitude == copy.latitude && c.longitude == copy.longitude);
    cJSON_Delete(o);
}
