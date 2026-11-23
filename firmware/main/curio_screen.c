#include "curio_screen.h"

#include <string.h>
#include "bsp/display.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

static const char *TAG = "curio_screen";
static lv_draw_buf_t *snapshot;
static uint32_t snapshot_size;
static SemaphoreHandle_t capture_done;
static lv_timer_t *capture_timer;
static esp_iris_media_desc_t capture_request;
static esp_err_t capture_result;

static esp_err_t take_snapshot(void)
{
    lv_display_t *display = bsp_display_get();
    if (!display) return ESP_ERR_INVALID_STATE;
    int32_t width = lv_display_get_horizontal_resolution(display);
    int32_t height = lv_display_get_vertical_resolution(display);
    if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX ||
        (capture_request.width && capture_request.width != width) ||
        (capture_request.height && capture_request.height != height)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    snapshot = lv_snapshot_take(lv_display_get_screen_active(display), LV_COLOR_FORMAT_RGB565);
    if (!snapshot) return ESP_ERR_NO_MEM;
    uint64_t size = (uint64_t)snapshot->header.stride * snapshot->header.h;
    if (snapshot->header.w != (uint32_t)width || snapshot->header.h != (uint32_t)height ||
        snapshot->header.stride < (uint32_t)width * 2 ||
        (capture_request.stride && capture_request.stride != snapshot->header.stride) ||
        !size || size > UINT32_MAX || size > snapshot->data_size) {
        lv_draw_buf_destroy(snapshot);
        snapshot = NULL;
        return ESP_ERR_NOT_SUPPORTED;
    }
    snapshot_size = (uint32_t)size;
    return ESP_OK;
}

static void capture_on_display(lv_timer_t *timer)
{
    capture_timer = NULL;
    lv_timer_delete(timer);
    capture_result = take_snapshot();
    xSemaphoreGive(capture_done);
}

static esp_err_t screen_begin(const esp_iris_media_desc_t *requested,
                              esp_iris_media_desc_t *actual,
                              uint32_t *total_size, void *user_ctx)
{
    (void)user_ctx;
    if (requested->format && requested->format != ESP_IRIS_PIXEL_FORMAT_RGB565)
        return ESP_ERR_NOT_SUPPORTED;
    if (requested->x || requested->y) return ESP_ERR_NOT_SUPPORTED;
    if (!bsp_display_lock(1000)) return ESP_ERR_TIMEOUT;
    if (snapshot || capture_timer) {
        bsp_display_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(capture_done, 0);
    capture_request = *requested;
    capture_result = ESP_ERR_TIMEOUT;
    /* Iris calls this on its small protocol stack. Render on the LVGL task. */
    capture_timer = lv_timer_create(capture_on_display, 1, NULL);
    bool scheduled = capture_timer != NULL;
    bsp_display_unlock();
    if (!scheduled) return ESP_ERR_NO_MEM;

    bool completed = xSemaphoreTake(capture_done, pdMS_TO_TICKS(2000)) == pdTRUE;
    bsp_display_lock(-1);
    if (capture_timer) {
        lv_timer_delete(capture_timer);
        capture_timer = NULL;
    }
    esp_err_t result = completed ? capture_result : ESP_ERR_TIMEOUT;
    if (result != ESP_OK) {
        if (snapshot) lv_draw_buf_destroy(snapshot);
        snapshot = NULL;
        snapshot_size = 0;
        bsp_display_unlock();
        return result;
    }
    *actual = (esp_iris_media_desc_t) {
        .width = snapshot->header.w,
        .height = snapshot->header.h,
        .stride = snapshot->header.stride,
        .format = ESP_IRIS_PIXEL_FORMAT_RGB565,
    };
    *total_size = snapshot_size;
    bsp_display_unlock();
    return ESP_OK;
}

static esp_err_t screen_read(uint32_t offset, uint8_t *out, size_t capacity,
                             size_t *out_size, void *user_ctx)
{
    (void)user_ctx;
    if (!snapshot || !out || !out_size || !capacity || offset >= snapshot_size)
        return ESP_ERR_INVALID_ARG;
    size_t size = snapshot_size - offset;
    if (size > capacity) size = capacity;
    memcpy(out, snapshot->data + offset, size);
    *out_size = size;
    return ESP_OK;
}

static void screen_end(void *user_ctx)
{
    (void)user_ctx;
    if (!snapshot) return;
    bsp_display_lock(-1);
    lv_draw_buf_destroy(snapshot);
    snapshot = NULL;
    snapshot_size = 0;
    bsp_display_unlock();
}

void curio_screen_start(void)
{
    const esp_iris_screen_backend_t backend = {
        .begin = screen_begin,
        .read = screen_read,
        .end = screen_end,
    };
    capture_done = xSemaphoreCreateBinary();
    if (!capture_done) {
        ESP_LOGW(TAG, "Screenshot completion allocation failed");
        return;
    }
    esp_err_t result = esp_iris_screen_register(&backend);
    if (result != ESP_OK) {
        vSemaphoreDelete(capture_done);
        capture_done = NULL;
        ESP_LOGW(TAG, "Screenshot registration failed: %s", esp_err_to_name(result));
    }
}
