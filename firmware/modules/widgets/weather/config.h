#pragma once
#include "curio_layout.h"
typedef struct { char city[65]; double latitude, longitude; } curio_weather_config_t;
extern const curio_widget_type_t curio_widget_weather;
