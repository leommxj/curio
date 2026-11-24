#include "view.h"
#include "curio_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void filter_click(lv_event_t *event)
{
    curio_widget_t *w = lv_event_get_user_data(event);
    agents_payload_t *a = w->data;
    for (size_t i = 0; i < 3; i++) if (lv_event_get_target(event) == a->filters[i]) a->filter = (curio_filter_t)i;
    a->page = 0;
    a->next_page_ms = w->now() + 10000;
    w->invalidate();
}
static void agent_swipe(lv_event_t *event)
{
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_dir_t direction = lv_indev_get_gesture_dir(input);
    lv_indev_wait_release(input);
    curio_widget_t *w = lv_event_get_user_data(event);
    agents_payload_t *a = w->data;
    a->next_page_ms = w->now() + 10000;
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) return;
    if (direction == LV_DIR_LEFT && a->page + 1 < a->pages) a->page++;
    else if (direction == LV_DIR_RIGHT && a->page) a->page--;
    w->invalidate();
}
static bool host_selected(const curio_widget_t *w, const char *id)
{
    const curio_agents_config_t *options = w->config.options;
    if (options->all_hosts) return true;
    for (size_t i = 0; i < options->host_count; i++) if (!strcmp(options->hosts[i], id)) return true;
    return false;
}
typedef struct {
    const curio_host_t *host;
    const curio_agent_t *agent;
} agent_row_t;

static int compare_rows(const void *a, const void *b)
{
    const agent_row_t *x = a, *y = b;
    int state = (int)x->agent->state - (int)y->agent->state;
    if (state) return state;
    int host = strcmp(x->host->host_id, y->host->host_id);
    return host ? host : strcmp(x->agent->id, y->agent->id);
}
static bool create(curio_widget_t *w)
{
    const curio_widget_config_t *config = &w->config;
    const curio_agents_config_t *options = config->options;
    curio_widget_rect_t rect = curio_widget_rect(config);
    size_t rows = (rect.height - 72) / 47;
    agents_payload_t *a = cJSON_malloc(sizeof(*a) + rows * sizeof(agent_view_row_t));
    if (!a) return false;
    memset(a, 0, sizeof(*a) + rows * sizeof(agent_view_row_t));
    w->data = a;
    int width = rect.width - 20;
    a->filter = options->filter;
    a->row_count = rows;
    a->next_page_ms = w->now() + 10000;
    lv_obj_remove_flag(w->box, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(w->box, agent_swipe, LV_EVENT_GESTURE, w);
    const char *filters[] = {"All", "Attention", "Working"};
    for (size_t i = 0; i < 3; i++) {
        int left = rect.width * i / 3, right = rect.width * (i + 1) / 3;
        a->filters[i] = curio_view_button(w->box, filters[i], left, 0, right - left - 4, filter_click, w);
        lv_obj_set_height(a->filters[i], 36);
        lv_obj_center(lv_obj_get_child(a->filters[i], 0));
    }
    for (size_t i = 0; i < rows; i++) {
        agent_view_row_t *row = &a->rows[i];
        row->name = curio_view_label(w->box, 10, 44 + i * 47, width - 130, &curio_font_18, CURIO_COLOR_FG);
        row->state = curio_view_label(w->box, rect.width - 130, 44 + i * 47, 120, &curio_font_18, CURIO_COLOR_MUTED);
        lv_obj_set_style_text_align(row->state, LV_TEXT_ALIGN_RIGHT, 0);
        row->detail = curio_view_label(w->box, 10, 65 + i * 47, width, &curio_font_14, CURIO_COLOR_MUTED);
    }
    a->status = curio_view_label(w->box, 10, rect.height - 40, width, &curio_font_14, 0xE8BD79);
    a->footer = curio_view_label(w->box, 10, rect.height - 22, width, &curio_font_14, CURIO_COLOR_MUTED);
    lv_obj_set_style_text_align(a->footer, LV_TEXT_ALIGN_CENTER, 0);
    return true;
}
static void sample(curio_widget_t *w, const curio_widget_sample_t *ctx)
{
    agents_payload_t *a = w->data;
    const curio_agents_config_t *options = w->config.options;
    agent_row_t rows[CURIO_MAX_HOSTS * CURIO_MAX_AGENTS];
    size_t count = 0;
    char missing[96] = "";
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++) {
        const curio_host_t *h = &ctx->core->hosts[i];
        if (!h->occupied || !host_selected(w, h->host_id)) continue;
        const curio_source_t *s = curio_host_source(h, options->source_id);
        const char *problem = curio_view_source_status(h, s, ctx->now);
        if (!problem && s && s->kind != CURIO_SOURCE_AGENTS) problem = "Type mismatch";
        if (problem && !missing[0]) snprintf(missing, sizeof(missing), "%s: %s", h->host_name, problem);
        if (!s || s->kind != CURIO_SOURCE_AGENTS) continue;
        for (size_t j = 0; j < s->agent_count; j++) {
            const curio_agent_t *agent = &s->agents[j];
            if ((a->filter == CURIO_FILTER_ATTENTION && agent->state != CURIO_STATE_BLOCKED) ||
                (a->filter == CURIO_FILTER_WORKING && agent->state != CURIO_STATE_WORKING) ||
                (options->project[0] && strcmp(options->project, agent->project)) ||
                (options->agent[0] && strcmp(options->agent, agent->id))) continue;
            rows[count++] = (agent_row_t){h, agent};
        }
    }
    if (!options->all_hosts) for (size_t i = 0; i < options->host_count; i++) {
        if (!curio_core_host(ctx->core, options->hosts[i]) && !missing[0]) snprintf(missing, sizeof(missing), "%s: Waiting", options->hosts[i]);
    }
    qsort(rows, count, sizeof(rows[0]), compare_rows);
    snprintf(a->summary, sizeof(a->summary), "%s", missing);
    a->count = count;
    size_t visible = a->row_count;
    size_t pages = count ? (count + visible - 1) / visible : 1;
    if (a->pages != pages || !options->auto_page || pages == 1 || ctx->touching)
        a->next_page_ms = ctx->now + 10000;
    a->pages = pages;
    if (a->page >= a->pages) a->page = a->pages - 1;
    if (ctx->paused) a->next_page_ms = ctx->now + 10000;
    if (!ctx->paused && options->auto_page && a->pages > 1 && ctx->now >= a->next_page_ms) {
        a->page = (a->page + 1) % a->pages;
        a->next_page_ms = ctx->now + 10000;
    }
    for (size_t i = 0; i < visible; i++) {
        agent_view_row_t *view = &a->rows[i];
        size_t index = a->page * visible + i;
        if (index >= count) {
            snprintf(view->name_text, sizeof(view->name_text), "%s", !i && !missing[0] ? "No matching agents" : "");
            view->detail_text[0] = 0;
            view->state_text[0] = 0;
            view->color = CURIO_COLOR_MUTED;
            continue;
        }
        const agent_row_t *row = &rows[index];
        const char *problem = curio_view_source_status(row->host, curio_host_source(row->host, options->source_id), ctx->now);
        const curio_agent_t *a = row->agent;
        const char *name = a->brand[0] && strcmp(a->name, a->brand) ? a->name : a->project[0] ? a->project : a->name;
        const char *brand = a->brand[0] ? a->brand : a->name;
        snprintf(view->name_text, sizeof(view->name_text), "%s", name);
        snprintf(view->state_text, sizeof(view->state_text), "%s", problem ? problem : curio_state_name(a->state));
        view->color = problem || a->state == CURIO_STATE_BLOCKED ? 0xE8BD79 :
            a->state == CURIO_STATE_WORKING ? 0x80C9B1 : CURIO_COLOR_MUTED;
        snprintf(view->detail_text, sizeof(view->detail_text), "%s | %s%s%s", brand, row->host->host_name,
            a->project[0] && strcmp(name, a->project) ? " | " : "", a->project[0] && strcmp(name, a->project) ? a->project : "");
    }
}
static void render(curio_widget_t *w)
{
    agents_payload_t *a = w->data;
    int available = curio_widget_rect(&w->config).width - 20;
    for (size_t i = 0; i < a->row_count; i++) {
        agent_view_row_t *row = &a->rows[i];
        int width = strlen(row->state_text) > 10 ? 170 : 120;
        lv_obj_set_width(row->name, available - width - 10);
        lv_obj_set_x(row->state, available + 10 - width);
        lv_obj_set_width(row->state, width);
        lv_obj_set_style_text_color(row->state, lv_color_hex(row->color), 0);
        curio_view_text(row->name, row->name_text);
        curio_view_text(row->state, row->state_text);
        curio_view_text(row->detail, row->detail_text);
    }
    curio_view_text(a->status, a->summary);
    char caption[64];
    snprintf(caption, sizeof(caption), "%u agents | %u / %u", (unsigned)a->count, (unsigned)a->page + 1, (unsigned)a->pages);
    curio_view_text(a->footer, caption);
    for (size_t i = 0; i < 3; i++) lv_obj_set_style_bg_color(a->filters[i], lv_color_hex(i == a->filter ? 0x26584F : 0x243243), 0);
}
const curio_widget_view_t curio_widget_agents_view = {.type = &curio_widget_agents, .create = create, .sample = sample, .render = render};
