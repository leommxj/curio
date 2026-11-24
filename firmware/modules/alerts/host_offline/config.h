#pragma once
#include "curio_alerts.h"
typedef struct { char host_id[49]; uint32_t hold_s; } host_offline_options_t;
extern const curio_alert_type_t curio_alert_host_offline;
