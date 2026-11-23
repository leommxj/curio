#pragma once
#include <stddef.h>
void *curio_yaml_malloc(size_t size);
void *curio_yaml_realloc(void *ptr, size_t size);
void curio_yaml_free(void *ptr);
char *curio_yaml_strdup(const char *text);
