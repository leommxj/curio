#pragma once
#include "curio_widget_view.h"
#include "config.h"
#include "format.h"
typedef struct {
    lv_obj_t *time, *date;
    curio_clock_text_t text;
} clock_payload_t;
