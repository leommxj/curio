#ifndef CURIO_LAYOUT_H
#define CURIO_LAYOUT_H
#include "curio_core.h"
#define CURIO_LAYOUT_MAX_PAGES 4
#define CURIO_LAYOUT_MAX_WIDGETS 8
#define CURIO_LAYOUT_COLUMNS 4
#define CURIO_LAYOUT_ROWS 6
typedef enum { CURIO_WIDGET_METRIC, CURIO_WIDGET_AGENTS, CURIO_WIDGET_MACHINE, CURIO_WIDGET_STATUS, CURIO_WIDGET_CLOCK } curio_widget_kind_t;
typedef struct {
    char id[33], title[49];
    curio_widget_kind_t type;
    unsigned col, row, cols, rows;
    char host_id[49], source_id[49], key[49], unit[17];
    int utc_offset_minutes;
    bool seconds;
    bool all_hosts;
    size_t host_count;
    char hosts[CURIO_MAX_HOSTS][49];
    curio_filter_t filter;
    bool auto_page;
    char project[49], agent[97];
} curio_widget_config_t;
typedef struct {
    char id[33], title[49];
    size_t widget_count;
    curio_widget_config_t widgets[CURIO_LAYOUT_MAX_WIDGETS];
} curio_page_config_t;
typedef struct {
    uint64_t revision;
    char default_page[33];
    size_t page_count;
    curio_page_config_t pages[CURIO_LAYOUT_MAX_PAGES];
} curio_display_config_t;
typedef struct { int x, y, width, height; } curio_widget_rect_t;
curio_widget_rect_t curio_widget_rect(const curio_widget_config_t *);
void curio_layout_default(curio_display_config_t *);
curio_result_t curio_layout_validate(const curio_display_config_t *);
#endif
