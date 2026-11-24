#include "curio_wifi.h"
#include "esp_wifi.h"
#include "mosaico_shared.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static wifi_config_t applied;
static unsigned connects, disconnects, writes;
static esp_err_t connect_result, config_result;
static const char *shared_ssid, *shared_password;
static unsigned shared_reads;

esp_err_t mosaico_shared_get(const char *key, char *value, size_t capacity)
{
    shared_reads++;
    const char *source = !strcmp(key, "wifi_ssid") ? shared_ssid :
                         !strcmp(key, "wifi_password") ? shared_password : NULL;
    if (!source) return ESP_ERR_NVS_NOT_FOUND;
    size_t size = strlen(source) + 1;
    if (size > capacity) return -1;
    memcpy(value, source, size);
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config)
{
    assert(interface == WIFI_IF_STA);
    applied = *config;
    writes++;
    return config_result;
}
esp_err_t esp_wifi_connect(void) { connects++; return connect_result; }
esp_err_t esp_wifi_disconnect(void) { disconnects++; return ESP_OK; }

static void reset(void)
{
    connects = disconnects = writes = 0;
    connect_result = config_result = ESP_OK;
    memset(&applied, 0, sizeof(applied));
}

static void test_shared(void)
{
    curio_network_config_t network = {.hostname = "curio"};
    shared_ssid = "Shared Home";
    shared_password = "shared-password";
    shared_reads = 0;
    assert(curio_wifi_load_shared(&network));
    assert(shared_reads == 2 && network.wifi_count == 1);
    assert(!strcmp(network.wifi[0].ssid, "Shared Home"));
    assert(!strcmp(network.wifi[0].password, "shared-password"));

    network = (curio_network_config_t){.hostname = "curio"};
    shared_password = NULL;
    assert(curio_wifi_load_shared(&network));
    assert(network.wifi_count == 1 && !*network.wifi[0].password);

    shared_ssid = NULL;
    assert(!curio_wifi_load_shared(&network));
    assert(network.wifi_count == 1 && !strcmp(network.wifi[0].ssid, "Shared Home"));
    shared_ssid = "";
    assert(!curio_wifi_load_shared(&network));
    shared_ssid = "Shared Home";
    shared_password = "short";
    assert(!curio_wifi_load_shared(&network));
    shared_password = "gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg";
    assert(!curio_wifi_load_shared(&network));
    shared_password = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    assert(curio_wifi_load_shared(&network));
    assert(!strcmp(network.wifi[0].password, shared_password));
    shared_ssid = "123456789012345678901234567890123";
    assert(!curio_wifi_load_shared(&network));
}

int main(void)
{
    test_shared();
    curio_wifi_t wifi = {0};
    curio_network_config_t network = {0};
    curio_wifi_update(&wifi, &network, 0, false, false);
    assert(!writes && !connects);

    network = (curio_network_config_t){.wifi_count = 2, .wifi = {
        {.ssid = "Home", .password = "home-password"},
        {.ssid = "Office", .password = "office-password"}}};
    curio_wifi_update(&wifi, &network, 0, false, false);
    assert(connects == 1 && !strcmp((char *)applied.sta.ssid, "Home"));
    assert(!strcmp((char *)applied.sta.password, "home-password") && applied.sta.pmf_cfg.capable);
    curio_wifi_update(&wifi, &network, 100, false, true);
    curio_wifi_update(&wifi, &network, 2099, false, false);
    assert(connects == 1);
    curio_wifi_update(&wifi, &network, 2100, false, false);
    assert(connects == 2 && !strcmp((char *)applied.sta.ssid, "Office"));
    curio_wifi_update(&wifi, &network, 2200, true, false);
    curio_wifi_update(&wifi, &network, 3600000, true, false);
    assert(connects == 2 && !disconnects);

    curio_wifi_update(&wifi, &network, 3600100, false, true);
    curio_wifi_update(&wifi, &network, 3602100, false, false);
    assert(connects == 3 && !strcmp((char *)applied.sta.ssid, "Office"));
    curio_wifi_update(&wifi, &network, 3602200, false, true);
    curio_wifi_update(&wifi, &network, 3604200, false, false);
    assert(connects == 4 && !strcmp((char *)applied.sta.ssid, "Home"));

    reset();
    wifi = (curio_wifi_t){0};
    curio_wifi_update(&wifi, &network, 0, false, false);
    curio_wifi_update(&wifi, &network, 14999, false, false);
    assert(connects == 1 && !disconnects);
    curio_wifi_update(&wifi, &network, 15000, false, false);
    assert(disconnects == 1 && connects == 1);
    curio_wifi_update(&wifi, &network, 15100, true, false);
    assert(wifi.phase == CURIO_WIFI_STOPPING);
    curio_wifi_update(&wifi, &network, 15200, false, true);
    curio_wifi_update(&wifi, &network, 16999, false, true);
    assert(connects == 1);
    curio_wifi_update(&wifi, &network, 17000, false, false);
    assert(connects == 2 && !strcmp((char *)applied.sta.ssid, "Office"));
    curio_wifi_update(&wifi, &network, 32000, false, false);
    curio_wifi_update(&wifi, &network, 34000, false, false);
    assert(connects == 3 && disconnects == 2 && !strcmp((char *)applied.sta.ssid, "Home"));

    reset();
    wifi = (curio_wifi_t){0};
    connect_result = -1;
    curio_wifi_update(&wifi, &network, 0, false, false);
    curio_wifi_update(&wifi, &network, 2000, false, false);
    assert(connects == 2 && !strcmp((char *)applied.sta.ssid, "Office"));
    config_result = -1;
    curio_wifi_update(&wifi, &network, 4000, false, false);
    assert(writes == 3 && connects == 2);
    connect_result = config_result = ESP_OK;
    curio_wifi_update(&wifi, &network, 6000, false, false);
    assert(connects == 3 && !strcmp((char *)applied.sta.ssid, "Office"));

    reset();
    wifi = (curio_wifi_t){0};
    network.wifi_count = 1;
    memset(network.wifi[0].ssid, 's', 32);
    network.wifi[0].ssid[32] = 0;
    memset(network.wifi[0].password, 'a', 64);
    network.wifi[0].password[64] = 0;
    curio_wifi_update(&wifi, &network, 0, false, false);
    assert(!memcmp(applied.sta.ssid, network.wifi[0].ssid, 32));
    assert(!memcmp(applied.sta.password, network.wifi[0].password, 64));
    curio_wifi_update(&wifi, &network, 100, false, true);
    curio_wifi_update(&wifi, &network, 2100, false, false);
    assert(connects == 2 && wifi.index == 0);
    puts("Wi-Fi: shared credentials, priority, failover, reconnect, DHCP timeout and driver failures passed");
}
