#include "view.h"
#include "curio_module.h"
#include <stdio.h>
#include <string.h>
static bool create(curio_widget_t *w)
{
    const curio_widget_config_t *config = &w->config;
    curio_widget_rect_t rect = curio_widget_rect(config);
    int width = rect.width - 20;
    metric_payload_t *m = cJSON_malloc(sizeof(*m));
    if (!m) return false;
    memset(m, 0, sizeof(*m));
    w->data = m;
    bool narrow = config->cols == 1;
    int top = config->rows == 1 ? 0 : (rect.height - (narrow ? 78 : 61)) / 2;
    curio_view_text(curio_view_label(w->box, narrow ? 8 : 10, top + 6, narrow ? rect.width - 16 : width / 2 - 8,
        &curio_font_14, CURIO_COLOR_MUTED), config->title);
    m->value = curio_view_label(w->box, narrow ? 8 : rect.width / 2, top + (narrow ? 25 : 3),
        narrow ? rect.width - 16 : rect.width / 2 - 10, &curio_font_26, CURIO_COLOR_FG);
    if (!narrow) lv_obj_set_style_text_align(m->value, LV_TEXT_ALIGN_RIGHT, 0);
    if (!narrow || config->rows > 1)
        m->detail = curio_view_label(w->box, narrow ? 8 : 10, top + (narrow ? 58 : 40), narrow ? rect.width - 16 : width,
            &curio_font_14, CURIO_COLOR_MUTED);
    return true;
}
static void sample(curio_widget_t *w, const curio_widget_sample_t *ctx)
{
    metric_payload_t *m = w->data;
    const curio_metric_config_t *c = w->config.options;
    curio_view_metric_text(ctx->core, m->value_text, sizeof(m->value_text), c->host_id, c->source_id, c->key, c->unit, ctx->now);
    const curio_host_t *h = curio_core_host(ctx->core, c->host_id);
    snprintf(m->detail_text, sizeof(m->detail_text), "%s", h ? h->host_name : c->host_id);
}
static void render(curio_widget_t *w)
{
    metric_payload_t *m = w->data;
    bool tiny = w->config.cols == 1 && w->config.rows == 1;
    curio_view_metric_value(m->value, m->value_text, tiny ? curio_widget_rect(&w->config).height - 27 : 0);
    if (m->detail) curio_view_text(m->detail, m->detail_text);
}
const curio_widget_view_t curio_widget_metric_view = {.type = &curio_widget_metric, .create = create, .sample = sample, .render = render};
