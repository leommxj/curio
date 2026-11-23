#pragma once
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, size_t item_size);
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
void vQueueDelete(QueueHandle_t queue);
