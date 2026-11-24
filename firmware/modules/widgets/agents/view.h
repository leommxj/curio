#pragma once
#include "curio_widget_view.h"
#include "config.h"
typedef struct {
    lv_obj_t *name, *state, *detail;
    char name_text[160], detail_text[224], state_text[24];
    uint32_t color;
} agent_view_row_t;
typedef struct {
    curio_filter_t filter;
    size_t page, pages, count;
    lv_obj_t *filters[3], *status, *footer;
    char summary[96];
    uint64_t next_page_ms;
    size_t row_count;
    agent_view_row_t rows[];
} agents_payload_t;
