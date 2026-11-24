#pragma once
#include "curio_layout.h"
typedef struct { int utc_offset_minutes; bool seconds; } curio_clock_config_t;
extern const curio_widget_type_t curio_widget_clock;
