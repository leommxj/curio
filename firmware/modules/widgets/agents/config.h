#pragma once
#include "curio_layout.h"
typedef struct { char source_id[49]; bool all_hosts; size_t host_count; char hosts[CURIO_MAX_HOSTS][49]; curio_filter_t filter; bool auto_page; char project[49], agent[97]; } curio_agents_config_t;
extern const curio_widget_type_t curio_widget_agents;
