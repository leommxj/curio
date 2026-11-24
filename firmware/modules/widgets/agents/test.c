#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void test_widget_agents(void)
{
    curio_display_config_t config = {0};
    assert(curio_layout_default(&config));
    curio_widget_config_t *agents = &config.pages[0].widgets[0];
    const curio_agents_config_t *defaults = agents->options;
    assert(defaults->all_hosts && defaults->auto_page && !strcmp(defaults->source_id, "herdr"));
    ((curio_agents_config_t *)agents->options)->all_hosts = false;
    ((curio_agents_config_t *)agents->options)->host_count = 2;
    strcpy(((curio_agents_config_t *)agents->options)->hosts[0], "one");
    strcpy(((curio_agents_config_t *)agents->options)->hosts[1], "two");
    assert(curio_layout_validate(&config) == CURIO_OK);
    strcpy(((curio_agents_config_t *)agents->options)->hosts[1], "one");
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    ((curio_agents_config_t *)agents->options)->host_count = 1;
    ((curio_agents_config_t *)agents->options)->all_hosts = true;
    assert(curio_layout_validate(&config) == CURIO_INVALID);
    curio_layout_free(&config);
}
