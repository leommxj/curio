#pragma once
#include <assert.h>
typedef void *SemaphoreHandle_t;
extern int mutex_depth;
static inline void xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    (void)mutex; (void)timeout;
    assert(mutex_depth == 0);
    mutex_depth++;
}
static inline void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    (void)mutex;
    assert(mutex_depth == 1);
    mutex_depth--;
}
