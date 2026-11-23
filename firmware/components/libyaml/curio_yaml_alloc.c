#include "curio_yaml_alloc.h"
#include "esp_heap_caps.h"
#include <string.h>
void *curio_yaml_malloc(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void *curio_yaml_realloc(void *ptr, size_t size) { return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void curio_yaml_free(void *ptr) { heap_caps_free(ptr); }
char *curio_yaml_strdup(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = curio_yaml_malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}
