#pragma once
#include "curio_alerts.h"
typedef struct { char host_id[49], source_id[49]; bool include_idle; } agent_done_options_t;
extern const curio_alert_type_t curio_alert_agent_done;
