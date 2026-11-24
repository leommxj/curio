#include "view.h"
#include "curio_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool create(curio_widget_t *w)
{
    const curio_widget_config_t *config = &w->config;
    const curio_clock_config_t *options = config->options;
    curio_widget_rect_t rect = curio_widget_rect(config);
    clock_payload_t *c = cJSON_malloc(sizeof(*c));
    if (!c) return false;
    memset(c, 0, sizeof(*c));
    w->data = c;
    char title[32];
    const char *caption = config->title;
    if (!*caption) {
        int offset = abs(options->utc_offset_minutes);
        char sign = options->utc_offset_minutes < 0 ? '-' : '+';
        if (!offset) snprintf(title, sizeof(title), "Time (UTC)");
        else if (offset % 60) snprintf(title, sizeof(title), "Time (UTC%c%d:%02d)", sign, offset / 60, offset % 60);
        else snprintf(title, sizeof(title), "Time (UTC%c%d)", sign, offset / 60);
        lv_point_t size;
        lv_text_get_size(&size, title, &curio_font_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        caption = size.x <= rect.width - 16 ? title : "Time";
    }
    curio_view_text(curio_view_label(w->box, 8, 6, rect.width - 16, &curio_font_14, CURIO_COLOR_MUTED), caption);
    bool date = config->rows > 1;
    int available_height = rect.height - (date ? 56 : 28);
    const lv_font_t *fonts[] = {&curio_font_48, &curio_font_32,
        &curio_font_26, &curio_font_18, &curio_font_14};
    const lv_font_t *font = fonts[4];
    for (size_t i = 0; i < 5; i++) {
        lv_point_t size;
        lv_text_get_size(&size, options->seconds ? "88:88:88" : "88:88", fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= rect.width - 16 && size.y <= available_height) { font = fonts[i]; break; }
    }
    int y = 25 + (available_height - lv_font_get_line_height(font)) / 2;
    c->time = curio_view_label(w->box, 8, y, rect.width - 16, font, CURIO_COLOR_FG);
    lv_obj_set_style_text_align(c->time, LV_TEXT_ALIGN_CENTER, 0);
    if (date) {
        c->date = curio_view_label(w->box, 8, rect.height - 25, rect.width - 16, &curio_font_14, CURIO_COLOR_MUTED);
        lv_obj_set_style_text_align(c->date, LV_TEXT_ALIGN_CENTER, 0);
    }
    return true;
}
static void sample(curio_widget_t *w, const curio_widget_sample_t *ctx)
{
    clock_payload_t *c = w->data;
    const curio_clock_config_t *options = w->config.options;
    curio_clock_format(ctx->wall_time, options->utc_offset_minutes, options->seconds, &c->text);
}
static void render(curio_widget_t *w)
{
    clock_payload_t *c = w->data;
    curio_view_text(c->time, c->text.time);
    if (c->date) curio_view_text(c->date, c->text.date);
}
const curio_widget_view_t curio_widget_clock_view = {.type = &curio_widget_clock, .create = create, .sample = sample, .render = render};
