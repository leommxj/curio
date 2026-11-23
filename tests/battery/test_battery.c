#include "../../firmware/main/curio_battery.c"
#include <assert.h>
#include <string.h>

curio_app_t curio_app;
static uint64_t now = 1000;
static uint16_t registers[256];
static int fail_command = -1;
static unsigned device_count;

uint64_t curio_now_ms(void) { return now; }
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
i2c_master_bus_handle_t bsp_i2c_get_handle(void) { return (void *)1; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                   i2c_master_dev_handle_t *device)
{
    assert(bus == (void *)1 && config->device_address == 0x55);
    assert(config->dev_addr_length == 7 && config->scl_speed_hz == 400000);
    *device = (void *)2;
    device_count++;
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *command,
                                     size_t tx_size, uint8_t *out, size_t rx_size, int timeout)
{
    assert(device == (void *)2 && tx_size == 1 && rx_size == 2 && timeout == 100);
    if (*command == fail_command) return ESP_ERR_TIMEOUT;
    uint16_t value = registers[*command];
    out[0] = value & 255;
    out[1] = value >> 8;
    return ESP_OK;
}
static void tick(void)
{
    curio_battery_poll();
}

int main(void)
{
    registers[BQ_OPERATION] = BQ_INITIALIZED;
    registers[BQ_STATUS] = BQ_PRESENT;
    registers[BQ_SOC] = 70;
    registers[BQ_VOLTAGE] = 3920;
    registers[BQ_CURRENT] = (uint16_t)-187;
    registers[BQ_AVERAGE_CURRENT] = (uint16_t)-174;
    registers[BQ_REMAINING] = 56;
    registers[BQ_FULL] = 80;
    registers[BQ_DESIGN] = 80;
    tick();
    curio_battery_t *b = &curio_app.battery;
    assert(b->valid && b->present && b->level_pct == 70);
    assert(b->current_ma == -187 && b->average_current_ma == -174);
    assert(b->voltage_mv == 3920 && b->remaining_capacity_mah == 56);
    assert(b->full_charge_capacity_mah == 80 && b->design_capacity_mah == 80);
    assert(curio_battery_state(b, now) == CURIO_BATTERY_DISCHARGING);
    char json[320];
    curio_battery_json(b, now, json, sizeof(json));
    assert(strstr(json, "\"current_ma\":-187"));
    assert(strstr(json, "\"status\":\"discharging\""));
    assert(strstr(json, "\"sample_age_ms\":0}"));
    for (int command = 0; command < 256; command++) {
        fail_command = command;
        if (command != BQ_OPERATION && command != BQ_STATUS && command != BQ_SOC &&
            command != BQ_VOLTAGE && command != BQ_CURRENT && command != BQ_AVERAGE_CURRENT &&
            command != BQ_REMAINING && command != BQ_FULL && command != BQ_DESIGN) continue;
        tick();
        assert(!b->valid && curio_battery_state(b, now) == CURIO_BATTERY_UNAVAILABLE);
        curio_battery_json(b, now, json, sizeof(json));
        assert(!strcmp(json, "{\"status\":\"unavailable\",\"level_pct\":null}"));
    }
    fail_command = -1;
    registers[BQ_SOC] = 0;
    tick();
    assert(b->valid && b->level_pct == 0);
    registers[BQ_SOC] = 101;
    tick();
    assert(!b->valid);
    registers[BQ_SOC] = 100;
    registers[BQ_OPERATION] |= BQ_CONFIG_UPDATING;
    tick();
    assert(!b->valid);
    registers[BQ_OPERATION] = 0;
    tick();
    assert(!b->valid);
    registers[BQ_OPERATION] = BQ_INITIALIZED;
    registers[BQ_STATUS] |= BQ_FULL_CHARGED;
    tick();
    assert(curio_battery_state(b, now) == CURIO_BATTERY_DISCHARGING);
    registers[BQ_CURRENT] = 15;
    tick();
    assert(curio_battery_state(b, now) == CURIO_BATTERY_CHARGING);
    registers[BQ_CURRENT] = 0;
    tick();
    assert(curio_battery_state(b, now) == CURIO_BATTERY_FULL);
    registers[BQ_STATUS] = BQ_PRESENT;
    tick();
    assert(curio_battery_state(b, now) == CURIO_BATTERY_IDLE);
    assert(curio_battery_state(b, now + 15000) == CURIO_BATTERY_IDLE);
    assert(curio_battery_state(b, now + 15001) == CURIO_BATTERY_UNAVAILABLE);
    registers[BQ_STATUS] = 0;
    registers[BQ_FULL] = 0;
    tick();
    assert(curio_battery_state(b, now) == CURIO_BATTERY_ABSENT);
    curio_battery_json(b, now, json, sizeof(json));
    assert(!strcmp(json, "{\"status\":\"absent\",\"level_pct\":null}"));
    assert(device_count == 1);
    puts("Battery signed current, charging/full/idle, stale/absent, invalid samples and I2C errors passed");
    return 0;
}
