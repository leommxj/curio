#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void test_widget_clock(void)
{
    curio_display_config_t config = {0};
    assert(curio_layout_default(&config));
    curio_widget_config_t *w = &config.pages[0].widgets[0];
    curio_widget_config_free(w);
    w->type = curio_widget_type_find("clock");
    w->options = calloc(1, w->type->options_size);
    assert(w->options);
    strcpy(w->id, "clock");
    w->cols = w->rows = 1;
    ((curio_clock_config_t *)w->options)->utc_offset_minutes = 840;
    assert(curio_layout_validate(&config) == CURIO_OK);
    ((curio_clock_config_t *)w->options)->utc_offset_minutes = -720;
    assert(curio_layout_validate(&config) == CURIO_OK);
    ((curio_clock_config_t *)w->options)->utc_offset_minutes = -721;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    ((curio_clock_config_t *)w->options)->utc_offset_minutes = 0;
    free(w->options);
    w->options = NULL;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    curio_layout_free(&config);
}
