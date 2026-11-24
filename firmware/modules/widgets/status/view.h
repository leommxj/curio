#pragma once
#include "curio_widget_view.h"
#include "config.h"
typedef struct {
    lv_obj_t *value, *detail;
    char value_text[65], detail_text[161];
    uint32_t color;
} status_payload_t;
