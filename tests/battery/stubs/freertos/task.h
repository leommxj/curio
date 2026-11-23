#pragma once
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(unsigned ticks);
int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *, unsigned, void *);
