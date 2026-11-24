#include "curio_app.h"
#include "curio_sensors.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_iris.h"
#include "iris_ota_support.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include <stdio.h>

EXT_RAM_BSS_ATTR curio_app_t curio_app;

uint64_t curio_now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000;
}

void app_main(void)
{
    cJSON_Hooks hooks = {.malloc_fn = curio_alloc, .free_fn = heap_caps_free};
    cJSON_InitHooks(&hooks);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    iris_ota_support_start();
    curio_app.mutex = xSemaphoreCreateMutex();
    configASSERT(curio_app.mutex);
    curio_core_init_allocator(&curio_app.core, (curio_allocator_t){curio_alloc, heap_caps_free});
    esp_err_t result = curio_settings_init();
    if (result != ESP_OK) {
        snprintf(curio_app.network_error, sizeof(curio_app.network_error), "Settings: %s", esp_err_to_name(result));
        ESP_LOGE("curio", "Settings unavailable: %s", esp_err_to_name(result));
    }
    curio_ui_start();
    curio_sensors_start();
    if (result == ESP_OK) {
        curio_provision_start();
        curio_network_start();
    }
}
