#include "curio_layout.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

static curio_display_config_t config;

static void test_default(void)
{
    curio_layout_default(&config);
    assert(curio_layout_validate(&config) == CURIO_OK);
    const curio_widget_config_t *w = &config.pages[0].widgets[0];
    assert(w->type == CURIO_WIDGET_AGENTS && w->auto_page && w->all_hosts);
    assert(!strcmp(w->source_id, "herdr"));
    assert(w->col == 0 && w->row == 0 && w->cols == 4 && w->rows == 6);
    curio_widget_rect_t rect = curio_widget_rect(w);
    assert(rect.x == 12 && rect.y == 60 && rect.width == 456 && rect.height == 408);
}

static void test_geometry(void)
{
    curio_widget_config_t w = {.col = 1, .row = 1, .cols = 1, .rows = 1};
    curio_widget_rect_t rect = curio_widget_rect(&w);
    assert(rect.x == 128 && rect.y == 129 && rect.width == 108 && rect.height == 61);
    w = (curio_widget_config_t){.col = 1, .row = 2, .cols = 3, .rows = 4};
    rect = curio_widget_rect(&w);
    assert(rect.x == 128 && rect.y == 198 && rect.width == 340 && rect.height == 270);
    w = (curio_widget_config_t){.col = 3, .row = 5, .cols = 1, .rows = 1};
    rect = curio_widget_rect(&w);
    assert(rect.x == 360 && rect.y == 406 && rect.width == 108 && rect.height == 62);
}

static void test_dimensions(void)
{
    curio_layout_default(&config);
    curio_widget_config_t *w = &config.pages[0].widgets[0];
    w->cols = w->rows = 3;
    w->col = 1;
    w->row = 3;
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->cols = 2;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->cols = 3;
    w->rows = 2;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->type = CURIO_WIDGET_MACHINE;
    strcpy(w->host_id, "one");
    w->cols = w->rows = 2;
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->rows = 1;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->rows = 2;
    w->cols = 1;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->type = CURIO_WIDGET_METRIC;
    strcpy(w->source_id, "system.cpu");
    strcpy(w->key, "usage_pct");
    w->cols = w->rows = 1;
    w->col = 3;
    w->row = 5;
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->cols = 0;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->cols = 1;
    w->rows = 0;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->rows = 1;
    w->col = 4;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->col = 3;
    w->row = 6;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->row = 5;
    w->cols = 2;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->cols = 1;
    w->rows = 2;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->rows = UINT_MAX;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->rows = 1;
    w->col = UINT_MAX;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

static void test_overlap_and_ids(void)
{
    curio_layout_default(&config);
    curio_page_config_t *page = &config.pages[0];
    curio_widget_config_t *agents = &page->widgets[0];
    agents->cols = agents->rows = 3;
    curio_widget_config_t *metric = &page->widgets[1];
    page->widget_count = 2;
    strcpy(metric->id, "cpu");
    metric->type = CURIO_WIDGET_METRIC;
    metric->col = 3;
    metric->cols = 1;
    metric->rows = 6;
    strcpy(metric->host_id, "never-seen");
    strcpy(metric->source_id, "system.cpu");
    strcpy(metric->key, "usage_pct");
    strcpy(metric->unit, "percent");
    assert(curio_layout_validate(&config) == CURIO_OK);
    metric->col = 2;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    metric->row = 3;
    metric->rows = 3;
    assert(curio_layout_validate(&config) == CURIO_OK);
    metric->row = 2;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    metric->row = 3;
    strcpy(metric->id, "agents");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(metric->id, "cpu");
    strcpy(metric->host_id, "bad host");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(metric->host_id, "never-seen");
    strcpy(metric->unit, "bad unit");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(metric->unit, "percent");
    config.page_count = 2;
    config.pages[1] = *page;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(config.pages[1].id, "second");
    assert(curio_layout_validate(&config) == CURIO_OK);
    strcpy(config.default_page, "absent");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

static void test_limits(void)
{
    curio_layout_default(&config);
    curio_page_config_t *page = &config.pages[0];
    page->widget_count = 8;
    for (unsigned i = 0; i < 8; i++) {
        curio_widget_config_t *w = &page->widgets[i];
        memset(w, 0, sizeof(*w));
        strcpy(w->id, "metric0");
        w->id[6] += (char)i;
        w->type = CURIO_WIDGET_METRIC;
        w->col = i % 4;
        w->row = i / 4;
        w->cols = w->rows = 1;
        strcpy(w->host_id, "one");
        strcpy(w->source_id, "system.cpu");
        strcpy(w->key, "usage_pct");
    }
    assert(curio_layout_validate(&config) == CURIO_OK);
    page->widget_count = 9;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    page->widget_count = 8;
    for (unsigned i = 1; i < 4; i++) {
        config.pages[i] = *page;
        strcpy(config.pages[i].id, "page0");
        config.pages[i].id[4] += (char)i;
    }
    config.page_count = 4;
    assert(curio_layout_validate(&config) == CURIO_OK);
    config.page_count = 5;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

static void test_status_binding(void)
{
    curio_layout_default(&config);
    curio_widget_config_t *w = &config.pages[0].widgets[0];
    w->type = CURIO_WIDGET_STATUS;
    w->cols = w->rows = 1;
    w->col = 3;
    w->row = 5;
    strcpy(w->host_id, "future-host");
    strcpy(w->source_id, "service.api");
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->source_id[0] = 0;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(w->source_id, "service.api");
    w->host_id[0] = 0;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

static void test_hosts(void)
{
    curio_layout_default(&config);
    curio_widget_config_t *agents = &config.pages[0].widgets[0];
    agents->all_hosts = false;
    agents->host_count = 2;
    strcpy(agents->hosts[0], "one");
    strcpy(agents->hosts[1], "two");
    assert(curio_layout_validate(&config) == CURIO_OK);
    strcpy(agents->hosts[1], "one");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    agents->host_count = 1;
    agents->all_hosts = true;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

static void test_clock(void)
{
    curio_layout_default(&config);
    curio_widget_config_t *w = &config.pages[0].widgets[0];
    *w = (curio_widget_config_t){.id = "clock", .type = CURIO_WIDGET_CLOCK, .cols = 1, .rows = 1, .utc_offset_minutes = 840};
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->utc_offset_minutes = -720;
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->utc_offset_minutes = -721;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->utc_offset_minutes = 0;
    strcpy(w->host_id, "one");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

int main(void)
{
    test_clock();
    test_default();
    test_geometry();
    test_dimensions();
    test_overlap_and_ids();
    test_limits();
    test_status_binding();
    test_hosts();
    assert(curio_layout_validate(NULL) == CURIO_INVALID);
    return 0;
}
