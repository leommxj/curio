#include "config.h"
#include "curio_module.h"
#include <string.h>

static bool validate(const void *options)
{
    const curio_machine_config_t *c = options;
    return curio_identifier(c->host_id, 48);
}
static bool parse(const cJSON *o, void *options)
{
    curio_machine_config_t *c = options;
    memset(c, 0, sizeof(*c));
    return curio_string(o, "host", c->host_id, sizeof(c->host_id), NULL) && validate(c);
}
static bool write(cJSON *o, const void *options)
{
    const curio_machine_config_t *c = options;
    return curio_put_string(o, "host", c->host_id);
}
const curio_widget_type_t curio_widget_machine = {
    .id = "machine", .title = "Machine", .fields = "host",
    .min_cols = 2, .min_rows = 2, .options_size = sizeof(curio_machine_config_t),
    .parse = parse, .validate = validate, .write = write,
};
