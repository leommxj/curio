#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "curio_core.h"
#include "curio_alerts.h"
#include "curio_battery.h"
#include "curio_orientation.h"
#include "curio_settings.h"

typedef struct {
    curio_core_t core;
    curio_display_config_t layout;
    curio_device_config_t device;
    curio_alert_config_t alert_config;
    curio_access_config_t access;
    curio_network_config_t network;
    curio_tls_config_t tls;
    curio_alerts_t *alerts;
    curio_battery_t battery;
    curio_orientation_status_t orientation;
    SemaphoreHandle_t mutex;
    bool configured;
    bool wifi_connected;
    bool https_ready;
    bool setup_active;
    char setup_ssid[24];
    char setup_password[17];
    char ip[16];
    char network_error[64];
} curio_app_t;

extern curio_app_t curio_app;

uint64_t curio_now_ms(void);
void curio_ui_start(void);
size_t curio_ui_widget_count(void);
void curio_network_start(void);
void curio_network_setup(bool enabled);
void curio_provision_start(void);
esp_err_t curio_restart(void);
