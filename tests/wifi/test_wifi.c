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
static esp_netif_ip_info_t ip_info;
static esp_netif_dns_info_t dns_info[3];
static bool dhcp = true;
static esp_err_t ip_result, dns_result, dhcp_result, dhcp_start_result;

esp_err_t esp_netif_dhcpc_stop(esp_netif_t *interface)
{
    (void)interface;
    if (dhcp_result) return dhcp_result;
    if (!dhcp) return ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;
    dhcp = false;
    return ESP_OK;
}
esp_err_t esp_netif_dhcpc_start(esp_netif_t *interface)
{
    (void)interface;
    if (dhcp_start_result) {
        if (dhcp_start_result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) dhcp = true;
        return dhcp_start_result;
    }
    if (dhcp) return ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED;
    memset(&dns_info[0], 0, 2 * sizeof(dns_info[0]));
    dhcp = true;
    return ESP_OK;
}
esp_err_t esp_netif_set_ip_info(esp_netif_t *interface, const esp_netif_ip_info_t *ip)
{
    (void)interface;
    assert(!dhcp);
    if (ip_result != ESP_OK) return ip_result;
    ip_info = *ip;
    memset(&dns_info[0], 0, 2 * sizeof(dns_info[0]));
    return ESP_OK;
}
esp_err_t esp_netif_set_dns_info(esp_netif_t *interface, int type, const esp_netif_dns_info_t *dns)
{
    (void)interface;
    if (!dns->ip.u_addr.ip4.addr) return ESP_ERR_ESP_NETIF_INVALID_PARAMS;
    if (dns_result != ESP_OK) return dns_result;
    dns_info[type] = *dns;
    return ESP_OK;
}
esp_err_t esp_netif_str_to_ip4(const char *s, esp_ip4_addr_t *out)
{
    unsigned a, b, c, d;
    assert(sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4);
    out->addr = (a << 24) | (b << 16) | (c << 8) | d;
    return ESP_OK;
}

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
    ip_result = dns_result = dhcp_result = dhcp_start_result = ESP_OK;
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
    esp_netif_dns_info_t empty_dns = {0};
    assert(esp_netif_set_dns_info(NULL, ESP_NETIF_DNS_MAIN, &empty_dns) == ESP_ERR_ESP_NETIF_INVALID_PARAMS);
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
    reset();
    wifi = (curio_wifi_t){0};
    network = (curio_network_config_t){.wifi_count = 2, .wifi = {
        {.ssid = "Static", .ipv4 = {.address = "192.168.1.50", .netmask = "255.255.255.0", .gateway = "192.168.1.1", .dns = "1.1.1.1"}},
        {.ssid = "DHCP"}}};
    dns_info[1].ip.u_addr.ip4.addr = 123;
    curio_wifi_update(&wifi, &network, 0, false, false);
    assert(!dhcp && ip_info.ip.addr == 0xc0a80132 && ip_info.netmask.addr == 0xffffff00 && ip_info.gw.addr == 0xc0a80101);
    assert(dns_info[0].ip.u_addr.ip4.addr == 0x01010101 && !dns_info[1].ip.u_addr.ip4.addr && !dns_info[2].ip.u_addr.ip4.addr);
    curio_wifi_update(&wifi, &network, 100, false, true);
    dns_info[1].ip.u_addr.ip4.addr = 123;
    curio_wifi_update(&wifi, &network, 2100, false, false);
    assert(dhcp && !ip_info.ip.addr && !ip_info.netmask.addr && !ip_info.gw.addr);
    assert(!dns_info[0].ip.u_addr.ip4.addr && !dns_info[1].ip.u_addr.ip4.addr && connects == 2);
    curio_wifi_update(&wifi, &network, 2200, false, true);
    curio_wifi_update(&wifi, &network, 4200, false, false);
    assert(!dhcp && ip_info.ip.addr == 0xc0a80132 && connects == 3);
    curio_wifi_update(&wifi, &network, 4300, false, true);
    ip_result = -1;
    curio_wifi_update(&wifi, &network, 6300, false, false);
    assert(connects == 3 && wifi.index == 0);
    ip_result = ESP_OK; dns_result = -1;
    curio_wifi_update(&wifi, &network, 8300, false, false);
    assert(connects == 3 && wifi.index == 1);
    dns_result = ESP_OK; dhcp_result = -1;
    curio_wifi_update(&wifi, &network, 10300, false, false);
    assert(connects == 3 && wifi.index == 0);
    dhcp_result = ESP_OK;
    curio_wifi_update(&wifi, &network, 12300, false, false);
    assert(connects == 4 && !dhcp);
    reset();
    wifi = (curio_wifi_t){0};
    network.wifi[0].ipv4 = (curio_wifi_ipv4_t){0};
    dhcp_start_result = -1;
    curio_wifi_update(&wifi, &network, 0, false, false);
    assert(!connects && wifi.index == 1);
    dhcp_start_result = ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED;
    curio_wifi_update(&wifi, &network, 2000, false, false);
    assert(connects == 1 && dhcp);
    puts("Wi-Fi: shared credentials, failover, reconnect, DHCP/static transitions and driver failures passed");
}
