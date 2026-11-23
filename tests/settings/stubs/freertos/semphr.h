#pragma once
#include <pthread.h>
#include <stdlib.h>
typedef pthread_mutex_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    SemaphoreHandle_t mutex = malloc(sizeof(*mutex));
    if (mutex) pthread_mutex_init(mutex, NULL);
    return mutex;
}
static inline void xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout) {
    (void)timeout;
    if (mutex) pthread_mutex_lock(mutex);
}
static inline void xSemaphoreGive(SemaphoreHandle_t mutex) {
    if (mutex) pthread_mutex_unlock(mutex);
}
