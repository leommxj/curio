#pragma once
#include "curio_layout.h"

typedef struct {
    void (*configure)(const curio_display_config_t *);
    void (*poll)(uint64_t now, bool connected);
} curio_widget_service_t;

extern const curio_widget_service_t *const curio_widget_services[];
extern const size_t curio_widget_service_count;
