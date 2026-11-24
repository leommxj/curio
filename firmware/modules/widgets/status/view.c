#include "view.h"
#include "curio_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool create(curio_widget_t *w)
{
    const curio_widget_config_t *config = &w->config;
    curio_widget_rect_t rect = curio_widget_rect(config);
    status_payload_t *s = cJSON_malloc(sizeof(*s));
    if (!s) return false;
    memset(s, 0, sizeof(*s));
    w->data = s;
    int width = rect.width - 20;
    bool narrow = config->cols == 1, short_row = config->rows == 1;
    curio_view_text(curio_view_label(w->box, 8, 6, narrow || !short_row ? rect.width - 16 : width / 2 - 8,
        &curio_font_14, CURIO_COLOR_MUTED), config->title);
    s->value = curio_view_label(w->box, narrow || !short_row ? 8 : rect.width / 2, narrow || !short_row ? 25 : 3,
        narrow || !short_row ? rect.width - 16 : rect.width / 2 - 10, &curio_font_26, CURIO_COLOR_FG);
    if (short_row && !narrow) lv_obj_set_style_text_align(s->value, LV_TEXT_ALIGN_RIGHT, 0);
    if (!short_row || !narrow) {
        int y = short_row ? 40 : 60;
        s->detail = curio_view_label(w->box, 8, y, rect.width - 16, &curio_font_14, CURIO_COLOR_MUTED);
        lv_label_set_long_mode(s->detail, short_row ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_WRAP);
        lv_obj_set_height(s->detail, rect.height - y - 6);
    }
    return true;
}
static void sample(curio_widget_t *w, const curio_widget_sample_t *ctx)
{
    const curio_status_config_t *options = w->config.options;
    status_payload_t *view = w->data;
    const curio_host_t *h = curio_core_host(ctx->core, options->host_id);
    const curio_source_t *s = curio_host_source(h, options->source_id);
    const char *problem = curio_view_source_status(h, s, ctx->now);
    if (!problem && s->kind != CURIO_SOURCE_STATUS) problem = "Type mismatch";
    if (!problem && !s->state) problem = "Unavailable";
    snprintf(view->value_text, sizeof(view->value_text), "%s", problem ? problem : s->state->text);
    snprintf(view->detail_text, sizeof(view->detail_text), "%s", problem ? "" : s->state->detail);
    view->color = problem ? CURIO_COLOR_MUTED : s->state->level == CURIO_STATUS_OK ? 0x80C9B1 :
        s->state->level == CURIO_STATUS_WARNING ? 0xE8BD79 : s->state->level == CURIO_STATUS_ERROR ? 0xEF9090 : CURIO_COLOR_FG;
}
static void render(curio_widget_t *w)
{
    status_payload_t *s = w->data;
    lv_point_t size;
    lv_text_get_size(&size, s->value_text, &curio_font_26, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_style_text_font(s->value, size.x > lv_obj_get_style_width(s->value, 0) ? &curio_font_18 : &curio_font_26, 0);
    lv_obj_set_style_text_color(s->value, lv_color_hex(s->color), 0);
    curio_view_text(s->value, s->value_text);
    if (s->detail) curio_view_text(s->detail, s->detail_text);
}
const curio_widget_view_t curio_widget_status_view = {.type = &curio_widget_status, .create = create, .sample = sample, .render = render};
