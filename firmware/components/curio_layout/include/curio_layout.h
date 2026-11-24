#pragma once
#include "curio_core.h"
#define CURIO_LAYOUT_MAX_PAGES 4
#define CURIO_LAYOUT_MAX_WIDGETS 8
#define CURIO_LAYOUT_COLUMNS 4
#define CURIO_LAYOUT_ROWS 6
#define CURIO_SERVICE_TIME 1U

typedef struct cJSON cJSON;
typedef struct {
    const char *id, *title, *fields;
    unsigned min_cols, min_rows, services;
    size_t options_size;
    bool (*parse)(const cJSON *, void *);
    bool (*validate)(const void *);
    bool (*write)(cJSON *, const void *);
    bool (*paging)(const void *);
    void (*set_paging)(void *, bool);
} curio_widget_type_t;
typedef struct {
    char id[33], title[49];
    const curio_widget_type_t *type;
    unsigned col, row, cols, rows;
    void *options;
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
extern const curio_widget_type_t *const curio_widget_types[];
extern const size_t curio_widget_type_count;
const curio_widget_type_t *curio_widget_type_find(const char *);
bool curio_widget_config_clone(curio_widget_config_t *, const curio_widget_config_t *);
void curio_widget_config_free(curio_widget_config_t *);
bool curio_page_clone(curio_page_config_t *, const curio_page_config_t *);
void curio_page_free(curio_page_config_t *);
bool curio_layout_clone(curio_display_config_t *, const curio_display_config_t *);
void curio_layout_free(curio_display_config_t *);
bool curio_layout_equal(const curio_display_config_t *, const curio_display_config_t *);
unsigned curio_layout_services(const curio_display_config_t *);
curio_widget_rect_t curio_widget_rect(const curio_widget_config_t *);
bool curio_layout_default(curio_display_config_t *);
curio_result_t curio_layout_validate(const curio_display_config_t *);
