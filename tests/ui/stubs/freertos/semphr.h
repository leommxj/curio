typedef void *SemaphoreHandle_t;
#define xSemaphoreTake(a, b) ((void)(a), (void)(b))
#define xSemaphoreGive(a) ((void)(a))
