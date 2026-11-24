#include "view.h"
#include "curio_module.h"
#include <stdio.h>
#include <string.h>
static bool create(curio_widget_t *w)
{
    const curio_widget_config_t *config = &w->config;
    curio_widget_rect_t rect = curio_widget_rect(config);
    int width = rect.width - 20;
    machine_payload_t *m = cJSON_malloc(sizeof(*m));
    if (!m) return false;
    memset(m, 0, sizeof(*m));
    w->data = m;
    lv_obj_t *title = curio_view_label(w->box, 10, 6, width, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
    curio_view_text(title, config->title);
    for (size_t i = 0; i < 2; i++) {
        int y = 28 + i * (rect.height - 50) / 2;
        curio_view_text(curio_view_label(w->box, 10, y + 6, 60, &lv_font_montserrat_14, CURIO_COLOR_MUTED), i ? "RAM" : "CPU");
        m->value[i] = curio_view_label(w->box, 72, y, width - 62, &lv_font_montserrat_26, CURIO_COLOR_FG);
        lv_obj_set_style_text_align(m->value[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    m->detail = curio_view_label(w->box, 10, rect.height - 22, width, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
    return true;
}
static void sample(curio_widget_t *w, const curio_widget_sample_t *ctx)
{
    machine_payload_t *m = w->data;
    const curio_machine_config_t *c = w->config.options;
    for (size_t i = 0; i < 2; i++)
        curio_view_metric_text(ctx->core, m->values[i], sizeof(m->values[i]), c->host_id,
            i ? "system.memory" : "system.cpu", "usage_pct", "percent", ctx->now);
    snprintf(m->detail_text, sizeof(m->detail_text), "%s", c->host_id);
}
static void render(curio_widget_t *w)
{
    machine_payload_t *m = w->data;
    for (size_t i = 0; i < 2; i++) curio_view_metric_value(m->value[i], m->values[i], 0);
    if (m->detail) curio_view_text(m->detail, m->detail_text);
}
const curio_widget_view_t curio_widget_machine_view = {.type = &curio_widget_machine, .create = create, .sample = sample, .render = render};
