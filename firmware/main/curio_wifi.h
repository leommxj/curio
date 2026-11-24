#pragma once
#include "curio_config_model.h"

typedef enum { CURIO_WIFI_WAIT, CURIO_WIFI_CONNECTING, CURIO_WIFI_STOPPING, CURIO_WIFI_ONLINE } curio_wifi_phase_t;
typedef struct {
    curio_wifi_phase_t phase;
    size_t index;
    uint64_t deadline;
} curio_wifi_t;

bool curio_wifi_load_shared(curio_network_config_t *);
void curio_wifi_update(curio_wifi_t *, const curio_network_config_t *, uint64_t now,
                       bool connected, bool disconnected);
