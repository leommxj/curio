#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void test_widget_status(void)
{
    curio_display_config_t config = {0};
    assert(curio_layout_default(&config));
    curio_widget_config_t *w = &config.pages[0].widgets[0];
    curio_widget_config_free(w);
    strcpy(w->id, "status");
    w->type = curio_widget_type_find("status");
    w->options = calloc(1, w->type->options_size);
    assert(w->options);
    w->cols = w->rows = 1;
    w->col = 3;
    w->row = 5;
    strcpy(((curio_status_config_t *)w->options)->host_id, "future-host");
    strcpy(((curio_status_config_t *)w->options)->source_id, "service.api");
    assert(curio_layout_validate(&config) == CURIO_OK);
    ((curio_status_config_t *)w->options)->source_id[0] = 0;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    strcpy(((curio_status_config_t *)w->options)->source_id, "service.api");
    ((curio_status_config_t *)w->options)->host_id[0] = 0;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    curio_layout_free(&config);
}
