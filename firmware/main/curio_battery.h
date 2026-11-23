#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool valid, present, fully_charged;
    uint8_t level_pct;
    uint16_t voltage_mv;
    int16_t current_ma, average_current_ma;
    uint16_t remaining_capacity_mah, full_charge_capacity_mah, design_capacity_mah;
    uint64_t sampled_at_ms;
} curio_battery_t;

typedef enum {
    CURIO_BATTERY_UNAVAILABLE,
    CURIO_BATTERY_ABSENT,
    CURIO_BATTERY_CHARGING,
    CURIO_BATTERY_DISCHARGING,
    CURIO_BATTERY_FULL,
    CURIO_BATTERY_IDLE
} curio_battery_state_t;

static inline curio_battery_state_t curio_battery_state(const curio_battery_t *battery, uint64_t now)
{
    if (!battery->valid || now < battery->sampled_at_ms || now - battery->sampled_at_ms > 15000)
        return CURIO_BATTERY_UNAVAILABLE;
    if (!battery->present) return CURIO_BATTERY_ABSENT;
    if (battery->current_ma > 2) return CURIO_BATTERY_CHARGING;
    if (battery->current_ma < -2) return CURIO_BATTERY_DISCHARGING;
    if (battery->fully_charged) return CURIO_BATTERY_FULL;
    return CURIO_BATTERY_IDLE;
}

void curio_battery_poll(void);
void curio_battery_json(const curio_battery_t *battery, uint64_t now, char *out, size_t capacity);
