#include "view.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static bool create(curio_widget_t *w)
{
    const curio_weather_config_t *c = w->config.options;
    curio_widget_rect_t rect = curio_widget_rect(&w->config);
    weather_payload_t *p = cJSON_malloc(sizeof(*p));
    if (!p) return false;
    memset(p, 0, sizeof(*p));
    w->data = p;
    int width = rect.width - 16;
    curio_view_text(curio_view_label(w->box, 8, 6, width, &curio_font_14, CURIO_COLOR_MUTED),
                    w->config.title[0] ? w->config.title : c->city);
    bool tall = w->config.rows > 1, wide = w->config.cols > 1;
    int y = tall && rect.height > 142 ? (rect.height - 86) / 2 : 25;
    p->temperature = curio_view_label(w->box, 8, y,
        width, tall && wide ? &curio_font_48 : &curio_font_26, CURIO_COLOR_FG);
    if (tall || wide) {
        p->condition = curio_view_label(w->box, 8, tall ? rect.height - 43 : 30, width, &curio_font_14, CURIO_COLOR_MUTED);
        if (!tall) {
            lv_obj_set_width(p->temperature, width / 2);
            lv_obj_set_pos(p->condition, rect.width / 2, 30);
            lv_obj_set_width(p->condition, width / 2);
            lv_obj_set_style_text_align(p->condition, LV_TEXT_ALIGN_RIGHT, 0);
        }
    }
    if (tall) p->humidity = curio_view_label(w->box, 8, rect.height - 23, width, &curio_font_14, CURIO_COLOR_MUTED);
    return true;
}
static void sample(curio_widget_t *w, const curio_widget_sample_t *ctx)
{
    const curio_weather_config_t *c = w->config.options;
    ((weather_payload_t *)w->data)->sample = curio_weather_read(c->latitude, c->longitude, ctx->now);
}
static void render(curio_widget_t *w)
{
    weather_payload_t *p = w->data;
    char temperature[24] = "--°", humidity[32] = "";
    if (p->sample.valid) {
        snprintf(temperature, sizeof(temperature), "%.0f°", p->sample.temperature);
        snprintf(humidity, sizeof(humidity), "%u%% RH", p->sample.humidity);
    }
    curio_view_text(p->temperature, temperature);
    lv_obj_set_style_text_color(p->temperature, lv_color_hex(p->sample.stale ? 0xE8BD79 : CURIO_COLOR_FG), 0);
    if (p->condition) curio_view_text(p->condition, !p->sample.valid ? (p->sample.stale ? "Unavailable" : "Waiting") :
        p->sample.stale ? "Stale" : curio_weather_description(p->sample.code));
    if (p->humidity) curio_view_text(p->humidity, humidity);
}
const curio_widget_view_t curio_widget_weather_view = {
    .type = &curio_widget_weather, .create = create, .sample = sample, .render = render,
};
