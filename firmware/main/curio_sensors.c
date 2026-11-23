#include "curio_sensors.h"
#include "curio_app.h"
#include "curio_orientation.h"
#include "bsp/display.h"
#include "bsp/imu.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "curio_sensors";
static curio_orientation_t orientation;
static curio_orientation_status_t status;
static bool imu_owned;
static uint64_t retry_at_ms;
static esp_err_t previous_imu_error, previous_rotation_error;

static esp_err_t stop_imu(void)
{
    if (!imu_owned) return ESP_OK;
    esp_err_t result = bsp_imu_deinit();
    if (result == ESP_OK) imu_owned = false;
    status.active = false;
    status.valid = false;
    curio_orientation_reset(&orientation);
    return result;
}

static esp_err_t start_imu(void)
{
    esp_err_t result = stop_imu();
    if (result != ESP_OK) return result;
    imu_owned = true;
    result = bsp_imu_init();
    bsp_imu_config_t config = BSP_IMU_CONFIG_DEFAULT();
    config.accel_odr = BMI2_ACC_ODR_25HZ;
    if (result == ESP_OK) result = bsp_imu_start(&config);
    uint8_t gyro = BMI2_GYRO;
    if (result == ESP_OK && bmi2_sensor_disable(&gyro, 1, bsp_imu_get_handle()) != BMI2_OK)
        result = ESP_FAIL;
    if (result == ESP_OK) status.active = true;
    else (void)stop_imu();
    return result;
}

static void sensors_poll(uint64_t now)
{
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    bool enabled = curio_app.device.auto_rotate;
    uint16_t manual_rotation = curio_app.device.rotation;
    xSemaphoreGive(curio_app.mutex);

    uint16_t rotation = (uint16_t)bsp_display_get_rotation();
    esp_err_t result = ESP_OK;
    if (!enabled) {
        result = stop_imu();
        retry_at_ms = 0;
        rotation = manual_rotation;
    } else {
        if (!status.active && now >= retry_at_ms) {
            result = start_imu();
            if (result != ESP_OK) retry_at_ms = now + 5000;
        }
        if (status.active) {
            result = bsp_imu_get_accel(&status.x_g, &status.y_g, &status.z_g);
            if (result == ESP_OK && (!isfinite(status.x_g) || !isfinite(status.y_g) || !isfinite(status.z_g)))
                result = ESP_ERR_INVALID_RESPONSE;
            status.valid = result == ESP_OK;
            if (status.valid) {
                status.sampled_at_ms = now;
                curio_orientation_update(&orientation, status.x_g, status.y_g, status.z_g, now, &rotation);
            } else {
                (void)stop_imu();
                retry_at_ms = now + 5000;
            }
        }
    }
    if (result != ESP_OK && result != previous_imu_error)
        ESP_LOGW(TAG, "IMU failed: %s", esp_err_to_name(result));
    if (!enabled || status.active || result != ESP_OK) previous_imu_error = result;

    if (rotation != (uint16_t)bsp_display_get_rotation()) {
        result = bsp_display_set_rotation((bsp_display_rotation_t)rotation);
        if (result != ESP_OK && result != previous_rotation_error)
            ESP_LOGW(TAG, "Rotation failed: %s", esp_err_to_name(result));
        previous_rotation_error = result;
    }
    status.rotation = (uint16_t)bsp_display_get_rotation();
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_app.orientation = status;
    xSemaphoreGive(curio_app.mutex);
}

static void sensors_task(void *arg)
{
    (void)arg;
    uint64_t battery_at_ms = 0;
    for (;;) {
        uint64_t now = curio_now_ms();
        if (now >= battery_at_ms) {
            curio_battery_poll();
            battery_at_ms = now + 5000;
        }
        sensors_poll(curio_now_ms());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void curio_sensors_start(void)
{
    status.rotation = (uint16_t)bsp_display_get_rotation();
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_app.orientation = status;
    xSemaphoreGive(curio_app.mutex);
    if (xTaskCreate(sensors_task, "curio_sensors", 4096, NULL, 3, NULL) != pdPASS)
        ESP_LOGW(TAG, "Task allocation failed");
}
