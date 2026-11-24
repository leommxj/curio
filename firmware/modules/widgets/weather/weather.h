#pragma once
#include "curio_layout.h"

typedef struct {
    double temperature;
    unsigned humidity, code;
    bool valid, stale;
} curio_weather_sample_t;

void curio_weather_configure(const curio_display_config_t *layout);
bool curio_weather_contains(double latitude, double longitude);
bool curio_weather_next(uint64_t now, double *latitude, double *longitude);
void curio_weather_complete(double latitude, double longitude, uint64_t now, const char *json);
curio_weather_sample_t curio_weather_read(double latitude, double longitude, uint64_t now);
const char *curio_weather_description(unsigned code);
