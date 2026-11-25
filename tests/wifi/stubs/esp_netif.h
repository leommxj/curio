#pragma once
#include "esp_wifi.h"
typedef struct esp_netif esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
typedef struct { struct { int type; union { esp_ip4_addr_t ip4; } u_addr; } ip; } esp_netif_dns_info_t;
#define ESP_IPADDR_TYPE_V4 0
#define ESP_NETIF_DNS_MAIN 0
#define ESP_NETIF_DNS_BACKUP 1
#define ESP_NETIF_DNS_FALLBACK 2
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED 101
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED 102
#define ESP_ERR_ESP_NETIF_INVALID_PARAMS 103
esp_err_t esp_netif_dhcpc_stop(esp_netif_t *);
esp_err_t esp_netif_dhcpc_start(esp_netif_t *);
esp_err_t esp_netif_set_ip_info(esp_netif_t *, const esp_netif_ip_info_t *);
esp_err_t esp_netif_set_dns_info(esp_netif_t *, int, const esp_netif_dns_info_t *);
esp_err_t esp_netif_str_to_ip4(const char *, esp_ip4_addr_t *);
