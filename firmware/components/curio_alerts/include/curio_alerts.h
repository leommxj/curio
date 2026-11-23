#ifndef CURIO_ALERTS_H
#define CURIO_ALERTS_H

#include "curio_core.h"

#define CURIO_ALERT_MAX_RULES 8
#define CURIO_ALERT_QUEUE 8

typedef enum {
    CURIO_ALERT_AGENT_DONE,
    CURIO_ALERT_METRIC_HIGH,
    CURIO_ALERT_HOST_OFFLINE
} curio_alert_type_t;

typedef enum { CURIO_ALERT_INFO, CURIO_ALERT_WARNING } curio_alert_kind_t;

typedef struct {
    char id[49];
    bool enabled;
    curio_alert_type_t type;
    char host_id[49];
    char source_id[49];
    char key[49];
    char unit[17];
    double threshold, recover;
    uint32_t hold_s;
    bool include_idle;
    uint16_t display_s;
    bool sound, vibrate, flash;
} curio_alert_rule_t;

typedef struct {
    uint32_t revision;
    uint8_t volume, vibration_strength;
    bool flash_enabled;
    size_t rule_count;
    curio_alert_rule_t rules[CURIO_ALERT_MAX_RULES];
} curio_alert_config_t;

typedef struct {
    uint32_t id;
    curio_alert_kind_t kind;
    char rule_id[49], host_id[49], agent_id[97];
    char title[33], body[97], detail[129];
    bool sound, vibrate, flash;
    uint16_t display_s;
    uint8_t volume, vibration_strength;
} curio_alert_notification_t;

typedef struct curio_alerts curio_alerts_t;

void curio_alert_config_default(curio_alert_config_t *config);
bool curio_alert_config_validate(const curio_alert_config_t *config);

curio_alerts_t *curio_alerts_create(curio_allocator_t allocator);
void curio_alerts_destroy(curio_alerts_t *alerts);
bool curio_alerts_configure(curio_alerts_t *alerts, const curio_alert_config_t *config);
uint32_t curio_alerts_revision(const curio_alerts_t *alerts);
void curio_alerts_set_vibration_strength(curio_alerts_t *alerts, uint8_t strength);
void curio_alerts_set_volume(curio_alerts_t *alerts, uint8_t volume);
void curio_alerts_tick(curio_alerts_t *alerts, const curio_core_t *core, uint64_t now_ms,
                       bool wifi_connected);
const curio_alert_notification_t *curio_alerts_current(const curio_alerts_t *alerts,
                                                        uint64_t now_ms);
uint64_t curio_alerts_remaining_ms(const curio_alerts_t *alerts, uint64_t now_ms);
void curio_alerts_dismiss(curio_alerts_t *alerts, uint64_t now_ms);
size_t curio_alerts_pending(const curio_alerts_t *alerts);
uint32_t curio_alerts_dropped(const curio_alerts_t *alerts);

#endif
