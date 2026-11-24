#ifndef CURIO_ALERTS_H
#define CURIO_ALERTS_H

#include "curio_core.h"

#define CURIO_ALERT_MAX_RULES 8
#define CURIO_ALERT_QUEUE 8

typedef enum { CURIO_ALERT_INFO, CURIO_ALERT_WARNING } curio_alert_kind_t;
typedef struct cJSON cJSON;
typedef struct curio_alert_type curio_alert_type_t;

typedef struct {
    char id[49];
    bool enabled;
    uint16_t display_s;
    bool sound, vibrate, flash;
    const curio_alert_type_t *type;
    void *options;
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

typedef void (*curio_alert_emit_fn)(void *, const char *, const curio_alert_notification_t *);
typedef void (*curio_alert_cancel_fn)(void *, const char *);
struct curio_alert_type {
    const char *id;
    const char *title;
    size_t options_size, state_size;
    const char *fields;
    bool (*parse)(const cJSON *, void *);
    bool (*validate)(const void *);
    bool (*write)(cJSON *, const void *);
    void (*describe)(const void *, char *, size_t);
    void (*evaluate)(void *, const void *, const curio_core_t *, uint64_t, bool,
                     curio_alert_emit_fn, curio_alert_cancel_fn, void *);
};
extern const curio_alert_type_t *const curio_alert_types[];
extern const size_t curio_alert_type_count;
const curio_alert_type_t *curio_alert_type_find(const char *id);
bool curio_alert_config_clone(curio_alert_config_t *dst, const curio_alert_config_t *src);
void curio_alert_config_free(curio_alert_config_t *config);
bool curio_alert_rules_equal(const curio_alert_config_t *a, const curio_alert_config_t *b);

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
