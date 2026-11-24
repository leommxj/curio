#pragma once
#include "curio_layout.h"
typedef struct { char host_id[49], source_id[49], key[49], unit[17]; } curio_metric_config_t;
extern const curio_widget_type_t curio_widget_metric;
