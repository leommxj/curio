#include "curio_layout.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include "../../firmware/modules/widgets/agents/config.h"
#include "../../firmware/modules/widgets/metric/config.h"
#include "../../firmware/modules/widgets/machine/config.h"
#include "../../firmware/modules/widgets/status/config.h"
#include "../../firmware/modules/widgets/clock/config.h"


static curio_display_config_t config;

static void reset(void)
{
    curio_layout_free(&config);
    assert(curio_layout_default(&config));
}

static void type(curio_widget_config_t *w, const char *id)
{
    free(w->options);
    w->type = curio_widget_type_find(id);
    assert(w->type);
    w->options = calloc(1, w->type->options_size);
    assert(w->options);
}


static void test_default(void)
{
    reset();
    assert(curio_layout_validate(&config) == CURIO_OK);
    const curio_widget_config_t *w = &config.pages[0].widgets[0];
    assert(w->type == curio_widget_type_find("agents"));
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
    reset();
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
    type(w, "machine");
    strcpy(((curio_machine_config_t *)w->options)->host_id, "one");
    w->cols = w->rows = 2;
    assert(curio_layout_validate(&config) == CURIO_OK);
    w->rows = 1;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    w->rows = 2;
    w->cols = 1;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    type(w, "metric");
    strcpy(((curio_metric_config_t *)w->options)->host_id, "one");
    strcpy(((curio_metric_config_t *)w->options)->source_id, "system.cpu");
    strcpy(((curio_metric_config_t *)w->options)->key, "usage_pct");
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
    reset();
    curio_page_config_t *page = &config.pages[0];
    curio_widget_config_t *agents = &page->widgets[0];
    agents->cols = agents->rows = 3;
    curio_widget_config_t *metric = &page->widgets[1];
    page->widget_count = 2;
    strcpy(metric->id, "cpu");
    type(metric, "metric");
    metric->col = 3;
    metric->cols = 1;
    metric->rows = 6;
    strcpy(((curio_metric_config_t *)metric->options)->host_id, "never-seen");
    strcpy(((curio_metric_config_t *)metric->options)->source_id, "system.cpu");
    strcpy(((curio_metric_config_t *)metric->options)->key, "usage_pct");
    strcpy(((curio_metric_config_t *)metric->options)->unit, "percent");
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
    strcpy(((curio_metric_config_t *)metric->options)->host_id, "bad host");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(((curio_metric_config_t *)metric->options)->host_id, "never-seen");
    strcpy(((curio_metric_config_t *)metric->options)->unit, "bad unit");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(((curio_metric_config_t *)metric->options)->unit, "percent");
    config.page_count = 2;
    assert(curio_page_clone(&config.pages[1], page));
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(config.pages[1].id, "second");
    assert(curio_layout_validate(&config) == CURIO_OK);
    strcpy(config.default_page, "absent");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
}

static void test_limits(void)
{
    reset();
    curio_page_config_t *page = &config.pages[0];
    curio_widget_config_free(&page->widgets[0]);
    page->widget_count = 8;
    for (unsigned i = 0; i < 8; i++) {
        curio_widget_config_t *w = &page->widgets[i];
        memset(w, 0, sizeof(*w));
        strcpy(w->id, "metric0");
        w->id[6] += (char)i;
        type(w, "metric");
        w->col = i % 4;
        w->row = i / 4;
        w->cols = w->rows = 1;
        strcpy(((curio_metric_config_t *)w->options)->host_id, "one");
        strcpy(((curio_metric_config_t *)w->options)->source_id, "system.cpu");
        strcpy(((curio_metric_config_t *)w->options)->key, "usage_pct");
    }
    assert(curio_layout_validate(&config) == CURIO_OK);
    page->widget_count = 9;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    page->widget_count = 8;
    for (unsigned i = 1; i < 4; i++) {
        assert(curio_page_clone(&config.pages[i], page));
        strcpy(config.pages[i].id, "page0");
        config.pages[i].id[4] += (char)i;
    }
    config.page_count = 4;
    assert(curio_layout_validate(&config) == CURIO_OK);
    config.page_count = 5;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    config.page_count = 4;
}




static void test_clone(void)
{
    reset();
    curio_display_config_t copy = {0};
    assert(curio_layout_clone(&copy, &config));
    assert(curio_layout_equal(&copy, &config));
    assert(copy.pages[0].widgets[0].options != config.pages[0].widgets[0].options);
    ((curio_agents_config_t *)copy.pages[0].widgets[0].options)->auto_page = false;
    assert(!curio_layout_equal(&copy, &config));
    assert(((curio_agents_config_t *)config.pages[0].widgets[0].options)->auto_page);
    curio_layout_free(&copy);
}

void curio_widget_module_tests(void);

int main(void)
{
    curio_widget_module_tests();
    test_clone();
    test_default();
    test_geometry();
    test_dimensions();
    test_overlap_and_ids();
    test_limits();
    assert(curio_layout_validate(NULL) == CURIO_INVALID);
    curio_layout_free(&config);
    return 0;
}
