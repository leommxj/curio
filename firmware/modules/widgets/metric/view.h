#pragma once
#include "curio_widget_view.h"
#include "config.h"
typedef struct {
    lv_obj_t *value, *detail;
    char value_text[160], detail_text[49];
} metric_payload_t;
