#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
