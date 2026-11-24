#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t request_id;
    uint32_t deadline_ms;
    const uint8_t *payload;
    size_t payload_size;
} esp_iris_rpc_request_t;
typedef esp_err_t (*esp_iris_rpc_handler_t)(const esp_iris_rpc_request_t *, uint8_t *, size_t, size_t *, void *);
esp_err_t esp_iris_rpc_register(uint16_t, uint16_t, esp_iris_rpc_handler_t, void *);
esp_err_t esp_iris_mark_planned_restart(void);
esp_err_t esp_iris_schedule_restart(uint32_t);
#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
