#pragma once
#include "curio_alerts.h"
typedef struct { char host_id[49], source_id[49], key[49], unit[17]; double threshold, recover; uint32_t hold_s; } metric_high_options_t;
extern const curio_alert_type_t curio_alert_metric_high;
