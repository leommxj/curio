#pragma once
#include "curio_widget_view.h"
#include "config.h"
#include "weather.h"

typedef struct {
    lv_obj_t *temperature, *condition, *humidity;
    curio_weather_sample_t sample;
} weather_payload_t;
