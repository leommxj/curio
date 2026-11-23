#include "curio_battery.h"
#include "curio_app.h"
#include "bsp/esp_mosaico.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdio.h>

enum {
    BQ_VOLTAGE = 0x08, BQ_STATUS = 0x0a, BQ_CURRENT = 0x0c,
    BQ_REMAINING = 0x10, BQ_FULL = 0x12, BQ_AVERAGE_CURRENT = 0x14,
    BQ_SOC = 0x2c, BQ_OPERATION = 0x3a, BQ_DESIGN = 0x3c
};
enum {
    BQ_PRESENT = 1u << 3, BQ_FULL_CHARGED = 1u << 9,
    BQ_INITIALIZED = 1u << 5, BQ_CONFIG_UPDATING = 1u << 10
};

static i2c_master_dev_handle_t gauge;
static const char *TAG = "curio_battery";

static esp_err_t attach(void)
{
    if (gauge) return ESP_OK;
    esp_err_t result = bsp_i2c_init();
    if (result != ESP_OK) return result;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_BATTERY_I2C_ADDR,
        .scl_speed_hz = BSP_BATTERY_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &gauge);
}

static esp_err_t read_word(uint8_t command, uint16_t *out)
{
    uint8_t bytes[2];
    esp_err_t result = i2c_master_transmit_receive(gauge, &command, 1, bytes, sizeof(bytes), 100);
    if (result == ESP_OK) *out = (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
    return result;
}

static esp_err_t sample(curio_battery_t *battery)
{
    // Read standard commands only; retain the gauge's capacity and calibration.
    uint16_t operation, flags, soc, voltage, current, average, remaining, full, design;
    const struct { uint8_t command; uint16_t *value; } reads[] = {
        {BQ_OPERATION, &operation}, {BQ_STATUS, &flags}, {BQ_SOC, &soc},
        {BQ_VOLTAGE, &voltage}, {BQ_CURRENT, &current}, {BQ_AVERAGE_CURRENT, &average},
        {BQ_REMAINING, &remaining}, {BQ_FULL, &full}, {BQ_DESIGN, &design}
    };
    for (size_t i = 0; i < sizeof(reads) / sizeof(reads[0]); i++) {
        esp_err_t result = read_word(reads[i].command, reads[i].value);
        if (result != ESP_OK) return result;
    }
    if (!(operation & BQ_INITIALIZED) || (operation & BQ_CONFIG_UPDATING)) return ESP_ERR_INVALID_STATE;
    if ((flags & BQ_PRESENT) && (soc > 100 || !voltage || !full))
        return ESP_ERR_INVALID_RESPONSE;
    *battery = (curio_battery_t){
        .valid = true, .present = (flags & BQ_PRESENT) != 0,
        .fully_charged = (flags & BQ_FULL_CHARGED) != 0, .level_pct = (uint8_t)soc,
        .voltage_mv = voltage, .current_ma = (int16_t)current,
        .average_current_ma = (int16_t)average, .remaining_capacity_mah = remaining,
        .full_charge_capacity_mah = full, .design_capacity_mah = design,
        .sampled_at_ms = curio_now_ms(),
    };
    return ESP_OK;
}

void curio_battery_poll(void)
{
    static esp_err_t previous = ESP_OK;
    curio_battery_t battery = {0};
    esp_err_t result = attach();
    if (result == ESP_OK) result = sample(&battery);
    if (result != ESP_OK && result != previous)
        ESP_LOGW(TAG, "Read failed: %s", esp_err_to_name(result));
    previous = result;
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_app.battery = battery;
    xSemaphoreGive(curio_app.mutex);
}

void curio_battery_json(const curio_battery_t *battery, uint64_t now, char *out, size_t capacity)
{
    static const char *names[] = {"unavailable", "absent", "charging", "discharging", "full", "idle"};
    curio_battery_state_t state = curio_battery_state(battery, now);
    if (state == CURIO_BATTERY_UNAVAILABLE || state == CURIO_BATTERY_ABSENT) {
        snprintf(out, capacity, "{\"status\":\"%s\",\"level_pct\":null}", names[state]);
        return;
    }
    snprintf(out, capacity,
        "{\"status\":\"%s\",\"level_pct\":%u,\"voltage_mv\":%u,\"current_ma\":%d,"
        "\"average_current_ma\":%d,\"remaining_capacity_mah\":%u,\"full_charge_capacity_mah\":%u,"
        "\"design_capacity_mah\":%u,\"sample_age_ms\":%lu}",
        names[state], battery->level_pct, battery->voltage_mv, battery->current_ma,
        battery->average_current_ma, battery->remaining_capacity_mah, battery->full_charge_capacity_mah,
        battery->design_capacity_mah, (unsigned long)(now - battery->sampled_at_ms));
}
