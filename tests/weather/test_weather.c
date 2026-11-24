#include "weather.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

const curio_widget_type_t curio_widget_weather = { .id = "weather" };
static const char *good = "{\"current\":{\"temperature_2m\":21.5,\"relative_humidity_2m\":64,\"weather_code\":3}}";

int main(void) {
    double latitude, longitude;
    assert(!curio_weather_next(0, &latitude, &longitude));
    curio_weather_config_t config = {.latitude = 1, .longitude = 2};
    curio_display_config_t layout = {.page_count = 1};
    layout.pages[0].widget_count = 2;
    for (int i = 0; i < 2; i++)
        layout.pages[0].widgets[i] = (curio_widget_config_t){.type = &curio_widget_weather, .options = &config};
    curio_weather_configure(&layout);
    assert(curio_weather_next(0, &latitude, &longitude) && latitude == 1 && longitude == 2);
    curio_weather_complete(1, 2, 100, good);
    assert(!curio_weather_next(900099, &latitude, &longitude));
    assert(curio_weather_next(900100, &latitude, &longitude));
    curio_weather_sample_t sample = curio_weather_read(1, 2, 100);
    assert(sample.valid && !sample.stale && sample.temperature == 21.5 && sample.humidity == 64 && sample.code == 3);
    assert(curio_weather_read(1, 2, 1800100).stale);
    curio_weather_configure(&layout);
    assert(curio_weather_read(1, 2, 101).valid);
    const char *bad[] = {NULL, "{}", "{\"current\":{\"temperature_2m\":null,\"relative_humidity_2m\":64,\"weather_code\":3}}", "{\"current\":{\"temperature_2m\":1e999,\"relative_humidity_2m\":64,\"weather_code\":3}}", "{\"current\":{\"temperature_2m\":1,\"relative_humidity_2m\":101,\"weather_code\":3}}", "{\"current\":{\"temperature_2m\":1,\"relative_humidity_2m\":64,\"weather_code\":3.5}}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        curio_weather_complete(1, 2, 200, bad[i]);
        sample = curio_weather_read(1, 2, 200);
        assert(sample.valid && sample.stale && sample.temperature == 21.5);
        assert(!curio_weather_next(60199, &latitude, &longitude));
        assert(curio_weather_next(60200, &latitude, &longitude));
    }
    curio_weather_complete(1, 2, 300, good);
    assert(!curio_weather_read(1, 2, 300).stale);
    config.latitude = 3;
    curio_weather_configure(&layout);
    curio_weather_complete(1, 2, 400, good);
    assert(!curio_weather_contains(1, 2));
    assert(!curio_weather_read(3, 2, 400).valid);
    assert(strcmp(curio_weather_description(95), "Thunderstorm") == 0);
    assert(strcmp(curio_weather_description(4), "Unknown") == 0);
    layout.page_count = 0;
    curio_weather_configure(&layout);
    assert(!curio_weather_next(1000000, &latitude, &longitude));
    puts("weather tests passed");
}
