#include "curio_core.h"
#include "curio_config_model.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

static _Thread_local char config_error[160];

void *curio_dev_create(void)
{
    curio_core_t *core = malloc(sizeof(*core));
    if (core) curio_core_init(core);
    return core;
}

void curio_dev_destroy(void *core)
{
    curio_core_destroy(core);
    free(core);
}

static curio_config_t *parse_config(const char *body, size_t size)
{
    curio_config_t *config = malloc(sizeof(*config));
    config_error[0] = 0;
    if (!config) return NULL;
    if (!curio_config_parse(body, size, config, config_error, sizeof(config_error))) {
        free(config);
        return NULL;
    }
    return config;
}

const char *curio_dev_config_error(void)
{
    return config_error;
}

char *curio_dev_config_yaml(const char *body, size_t size)
{
    curio_config_t *config = parse_config(body, size);
    if (!config) return NULL;
    char *yaml = curio_config_yaml(config);
    free(config);
    return yaml;
}

char *curio_dev_config_json(const char *body, size_t size)
{
    curio_config_t *config = parse_config(body, size);
    if (!config) return NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON *access = root ? cJSON_AddObjectToObject(root, "access") : NULL;
    bool ok = access && cJSON_AddStringToObject(access, "admin_token", config->access.admin_token) &&
        cJSON_AddStringToObject(access, "collector_token", config->access.collector_token);
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    free(config);
    return json;
}

char *curio_dev_config_token(const char *body, size_t size, const char *role, const char *token)
{
    curio_config_t *config = parse_config(body, size);
    if (!config) return NULL;
    char *target = !strcmp(role, "admin_token") ? config->access.admin_token : config->access.collector_token;
    size_t length = strlen(token);
    if (length < 16 || length > 128) { free(config); return NULL; }
    for (size_t i = 0; i < length; i++) if ((unsigned char)token[i] < 33 || (unsigned char)token[i] > 126) {
        free(config);
        return NULL;
    }
    memcpy(target, token, length + 1);
    char *yaml = curio_config_yaml(config);
    free(config);
    return yaml;
}
