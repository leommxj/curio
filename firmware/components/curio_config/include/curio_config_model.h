#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "curio_layout.h"
#include "curio_alerts.h"
#define CURIO_CONFIG_MAX_BYTES 16384

typedef struct {
    uint32_t revision;
    uint8_t brightness;
    uint16_t rotation;
    bool auto_rotate;
    char ntp_server[128];
} curio_device_config_t;
typedef struct { char admin_token[129], collector_token[129]; } curio_access_config_t;
typedef struct { char ssid[33], password[64], hostname[33]; } curio_network_config_t;
typedef struct { char certificate[2048], private_key[2048]; } curio_tls_config_t;
typedef struct {
    curio_device_config_t device;
    curio_display_config_t display;
    curio_alert_config_t alerts;
    curio_access_config_t access;
    curio_network_config_t network;
    curio_tls_config_t tls;
} curio_config_t;
/* Initialize configurations to zero; successful parsing replaces their owned contents. */
void curio_config_free(curio_config_t *);
bool curio_config_clone(curio_config_t *, const curio_config_t *);
bool curio_config_equal(const curio_config_t *, const curio_config_t *);
bool curio_config_parse(const char *yaml, size_t size, curio_config_t *config, char *error, size_t error_size);
/* Free the returned YAML with cJSON_free. */
char *curio_config_yaml(const curio_config_t *config);
