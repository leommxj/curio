#pragma once
#include "curio_layout.h"
#include "lvgl.h"
#include <time.h>

typedef struct curio_widget curio_widget_t;
typedef struct {
    const curio_core_t *core;
    uint64_t now;
    time_t wall_time;
    bool paused, touching;
} curio_widget_sample_t;
typedef struct {
    const curio_widget_type_t *type;
    bool (*create)(curio_widget_t *);
    void (*sample)(curio_widget_t *, const curio_widget_sample_t *);
    void (*render)(curio_widget_t *);
} curio_widget_view_t;
struct curio_widget {
    lv_obj_t *box;
    curio_widget_config_t config;
    const curio_widget_view_t *view;
    void *data;
    void (*invalidate)(void);
    uint64_t (*now)(void);
};
extern const curio_widget_view_t *const curio_widget_views[];
extern const size_t curio_widget_view_count;
curio_widget_t *curio_widget_create(lv_obj_t *, const curio_widget_config_t *, void (*)(void), uint64_t (*)(void));
void curio_widget_destroy(curio_widget_t *);
void curio_view_text(lv_obj_t *, const char *);
lv_obj_t *curio_view_label(lv_obj_t *, int, int, int, const lv_font_t *, uint32_t);
lv_obj_t *curio_view_button(lv_obj_t *, const char *, int, int, int, lv_event_cb_t, void *);
const char *curio_view_source_status(const curio_host_t *, const curio_source_t *, uint64_t);
void curio_view_metric_text(const curio_core_t *, char *, size_t, const char *, const char *, const char *, const char *, uint64_t);
void curio_view_metric_value(lv_obj_t *, const char *, int height);
#define CURIO_COLOR_FG 0xE8EFF7
#define CURIO_COLOR_MUTED 0x94A6BC
