#include "curio_widget_view.h"
#include "curio_module.h"
#include <stdio.h>
#include <string.h>

void curio_view_text(lv_obj_t *obj, const char *value)
{
    char ascii[192];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p && n + 1 < sizeof(ascii); p++) {
        if (*p < 128) ascii[n++] = *p >= 32 ? *p : ' ';
        else if ((*p & 0xC0) != 0x80) ascii[n++] = '?';
    }
    ascii[n] = 0;
    if (strcmp(lv_label_get_text(obj), ascii)) lv_label_set_text(obj, ascii);
}

lv_obj_t *curio_view_label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}

lv_obj_t *curio_view_button(lv_obj_t *parent, const char *caption, int x, int y, int width, lv_event_cb_t callback, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 44);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x243243), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    lv_obj_t *name = curio_view_label(obj, 0, 0, width, &lv_font_montserrat_14, CURIO_COLOR_FG);
    curio_view_text(name, caption);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(name);
    return obj;
}

const char *curio_view_source_status(const curio_host_t *h, const curio_source_t *s, uint64_t now)
{
    if (!h) return "Waiting";
    if (curio_host_health(h, now) == CURIO_HEALTH_OFFLINE) return "Offline";
    if (!s) return "Not reported";
    curio_health_t health = curio_source_health(s, now);
    if (health == CURIO_HEALTH_STALE) return "Stale";
    if (s->status == CURIO_SOURCE_ERROR) return "Collection error";
    if (s->status == CURIO_SOURCE_UNAVAILABLE) return "Unavailable";
    if (s->status == CURIO_SOURCE_WARMING_UP) return "Warming up";
    return NULL;
}

void curio_view_metric_text(const curio_core_t *core, char *out, size_t size, const char *host_id, const char *source_id, const char *key, const char *unit, uint64_t now)
{
    const curio_host_t *h = curio_core_host(core, host_id);
    const curio_source_t *s = h ? curio_host_source(h, source_id) : NULL;
    const char *problem = curio_view_source_status(h, s, now);
    const curio_metric_t *m = s && s->kind == CURIO_SOURCE_METRICS ? curio_source_metric(s, key) : NULL;
    if (!problem && s->kind != CURIO_SOURCE_METRICS) problem = "Type mismatch";
    if (!problem && !m) problem = "Not reported";
    if (!problem && unit[0] && strcmp(unit, m->unit)) problem = "Type mismatch";
    if (!problem && !m->has_value) problem = "Unavailable";
    if (!problem && curio_metric_age(m, now) > s->max_age_ms) problem = "Stale";
    if (problem) snprintf(out, size, "%s", problem);
    else curio_format_metric(out, size, m->value, m->unit);
}

curio_widget_t *curio_widget_create(lv_obj_t *parent, const curio_widget_config_t *config,
                                    void (*invalidate)(void), uint64_t (*now)(void))
{
    const curio_widget_view_t *view = NULL;
    for (size_t i = 0; i < curio_widget_view_count; i++)
        if (curio_widget_views[i]->type == config->type) view = curio_widget_views[i];
    if (!view) return NULL;
    curio_widget_t *w = cJSON_malloc(sizeof(*w));
    if (!w) return NULL;
    memset(w, 0, sizeof(*w));
    if (!curio_widget_config_clone(&w->config, config)) { cJSON_free(w); return NULL; }
    w->view = view;
    w->invalidate = invalidate;
    w->now = now;
    curio_widget_rect_t rect = curio_widget_rect(config);
    w->box = lv_obj_create(parent);
    lv_obj_set_pos(w->box, rect.x, rect.y);
    lv_obj_set_size(w->box, rect.width, rect.height);
    lv_obj_set_style_pad_all(w->box, 0, 0);
    lv_obj_set_style_border_width(w->box, 0, 0);
    lv_obj_set_style_bg_color(w->box, lv_color_hex(0x192534), 0);
    lv_obj_remove_flag(w->box, LV_OBJ_FLAG_SCROLLABLE);
    if (!view->create(w)) { curio_widget_destroy(w); return NULL; }
    return w;
}
void curio_widget_destroy(curio_widget_t *w)
{
    lv_obj_delete(w->box);
    cJSON_free(w->data);
    curio_widget_config_free(&w->config);
    cJSON_free(w);
}
void curio_view_metric_value(lv_obj_t *value, const char *caption, int height)
{
    bool number = (caption[0] >= '0' && caption[0] <= '9') || caption[0] == '-';
    const lv_font_t *font = number ? &lv_font_montserrat_26 : &lv_font_montserrat_18;
    lv_point_t size;
    lv_text_get_size(&size, caption, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x > lv_obj_get_style_width(value, 0)) font = &lv_font_montserrat_14;
    lv_obj_set_style_text_font(value, font, 0);
    lv_label_set_long_mode(value, height > 0 && !number ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_height(value, height > 0 ? height : LV_SIZE_CONTENT);
    curio_view_text(value, caption);
}
