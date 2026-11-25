#include "curio_app.h"
#include "curio_feedback.h"
#include "curio_clock.h"
#include "curio_wifi.h"
#include "curio_widget_service.h"

#include "curio_web_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include "cJSON.h"
#include "esp_event.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_iris.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "freertos/task.h"

#define NETWORK_CONNECTED 1U
#define NETWORK_DISCONNECTED 2U
#define NETWORK_SETUP_CHANGED 4U
#define NETWORK_SETUP_READY 8U

static const char *TAG = "curio_network";
static TaskHandle_t network_task_handle;
static httpd_handle_t server;
static httpd_handle_t setup_server;
static esp_netif_t *setup_interface;
static struct ifreq setup_interface_name;
static atomic_bool setup_requested;

static bool setup_request(httpd_req_t *req)
{
    return req->handle == setup_server;
}

static esp_err_t json_response(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t error_response(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root) cJSON_AddStringToObject(root, "error", message);
    char *json = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    esp_err_t result = json_response(req, status, json ? json : "{\"error\":\"request failed\"}");
    cJSON_free(json);
    return result;
}

static bool authorized(httpd_req_t *req, bool admin)
{
    char token[129];
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    snprintf(token, sizeof(token), "%s", admin ? curio_app.access.admin_token : curio_app.access.collector_token);
    xSemaphoreGive(curio_app.mutex);
    if (!token[0]) return false;
    size_t length = strlen(token);
    if (httpd_req_get_hdr_value_len(req, "Authorization") != length + 7) return false;
    char supplied[136];
    if (httpd_req_get_hdr_value_str(req, "Authorization", supplied, sizeof(supplied)) != ESP_OK) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < 7; i++) diff |= (unsigned char)supplied[i] ^ (unsigned char)"Bearer "[i];
    for (size_t i = 0; i < length; i++) diff |= (unsigned char)supplied[i + 7] ^ (unsigned char)token[i];
    return diff == 0;
}

static esp_err_t require_auth(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
    return error_response(req, "401 Unauthorized", "unauthorized");
}

static esp_err_t read_body(httpd_req_t *req, size_t maximum, char **body)
{
    *body = NULL;
    if (!req->content_len) return error_response(req, "400 Bad Request", "empty body");
    if (req->content_len > maximum) return error_response(req, "413 Content Too Large", "body too large");
    char *input = curio_alloc(req->content_len + 1);
    if (!input) return error_response(req, "503 Service Unavailable", "out of memory");
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, input + received, req->content_len - received);
        if (count <= 0) {
            free(input);
            error_response(req, "408 Request Timeout", "incomplete body");
            return ESP_FAIL;
        }
        received += count;
    }
    input[received] = 0;
    *body = input;
    return ESP_OK;
}

static esp_err_t snapshot_handler(httpd_req_t *req)
{
    if (!authorized(req, false)) return require_auth(req);
    char *body;
    esp_err_t response = read_body(req, CURIO_MAX_BODY_BYTES, &body);
    if (!body) return response;
    curio_snapshot_t *snapshot = NULL;
    curio_result_t result = curio_core_parse(&curio_app.core, body, req->content_len, &snapshot);
    free(body);
    if (result == CURIO_OK) {
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        result = curio_core_apply(&curio_app.core, snapshot, curio_now_ms());
        xSemaphoreGive(curio_app.mutex);
    }
    curio_core_snapshot_free(&curio_app.core, snapshot);
    switch (result) {
    case CURIO_OK: return json_response(req, "200 OK", "{\"ok\":true}");
    case CURIO_CAPACITY: return error_response(req, "503 Service Unavailable", "host capacity reached");
    case CURIO_NO_MEMORY: return error_response(req, "503 Service Unavailable", "out of memory");
    case CURIO_TOO_LARGE: return error_response(req, "413 Content Too Large", "snapshot too large");
    default: return error_response(req, "400 Bad Request", "invalid snapshot");
    }
}

static esp_err_t state_handler(httpd_req_t *req)
{
    if (!authorized(req, true)) return require_auth(req);
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    uint64_t now = curio_now_ms();
    char *json = !strcmp(req->uri, "/api/v2/catalog") ? curio_core_catalog_json(&curio_app.core, now) :
        curio_core_state_v2_json(&curio_app.core, now);
    xSemaphoreGive(curio_app.mutex);
    if (!json) return error_response(req, "503 Service Unavailable", "out of memory");
    esp_err_t result = json_response(req, "200 OK", json);
    curio_core_free_json(json);
    return result;
}

static esp_err_t config_response(httpd_req_t *req)
{
    uint32_t revision;
    char *yaml = curio_settings_yaml(&revision);
    if (!yaml) return error_response(req, "503 Service Unavailable", "configuration unavailable");
    char etag[24];
    snprintf(etag, sizeof(etag), "\"%lu\"", (unsigned long)revision);
    httpd_resp_set_type(req, "application/yaml; charset=utf-8");
    httpd_resp_set_hdr(req, "ETag", etag);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    esp_err_t result = httpd_resp_send(req, yaml, HTTPD_RESP_USE_STRLEN);
    free(yaml);
    return result;
}

static esp_err_t save_error(httpd_req_t *req, esp_err_t error, const char *detail)
{
    if (error == ESP_ERR_INVALID_VERSION) return error_response(req, "409 Conflict", "configuration changed");
    if (error == ESP_ERR_INVALID_ARG) return error_response(req, "400 Bad Request", detail && detail[0] ? detail : "invalid configuration");
    if (error == ESP_ERR_INVALID_SIZE) return error_response(req, "413 Content Too Large", "configuration too large");
    return error_response(req, "503 Service Unavailable", "configuration save failed");
}

static esp_err_t config_handler(httpd_req_t *req)
{
    if (!setup_request(req) && !authorized(req, true)) return require_auth(req);
    if (req->method == HTTP_GET) return config_response(req);
    char content_type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK)
        return error_response(req, "415 Unsupported Media Type", "Content-Type must be application/yaml");
    content_type[strcspn(content_type, "; ")] = 0;
    if (strcasecmp(content_type, "application/yaml") && strcasecmp(content_type, "text/yaml"))
        return error_response(req, "415 Unsupported Media Type", "Content-Type must be application/yaml");
    char etag[24];
    unsigned long revision = 0;
    size_t length;
    if (httpd_req_get_hdr_value_str(req, "If-Match", etag, sizeof(etag)) != ESP_OK ||
        (length = strlen(etag)) < 3 || length > 12 || etag[0] != '"' || etag[length - 1] != '"')
        return error_response(req, "428 Precondition Required", "If-Match required");
    for (size_t i = 1; i < length - 1; i++) {
        if (etag[i] < '0' || etag[i] > '9' || revision > (UINT32_MAX - 3 - (etag[i] - '0')) / 10)
            return error_response(req, "428 Precondition Required", "If-Match required");
        revision = revision * 10 + etag[i] - '0';
    }
    char *body;
    esp_err_t result = read_body(req, CURIO_CONFIG_MAX_BYTES, &body);
    if (!body) return result;
    char error[128] = "";
    result = curio_settings_import(body, req->content_len, (uint32_t)revision, error, sizeof(error));
    free(body);
    return result == ESP_OK ? config_response(req) : save_error(req, result, error);
}

static esp_err_t restart_handler(httpd_req_t *req)
{
    if (!setup_request(req) && !authorized(req, true)) return require_auth(req);
    esp_err_t result = curio_restart();
    return result == ESP_OK ? json_response(req, "200 OK", "{\"ok\":true}") :
        error_response(req, "503 Service Unavailable", "restart failed");
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    esp_err_t result = curio_settings_reset();
    return result == ESP_OK ? restart_handler(req) : save_error(req, result, NULL);
}

static esp_err_t token_handler(httpd_req_t *req)
{
    if (!authorized(req, true)) return require_auth(req);
    bool admin = req->method == HTTP_PUT;
    char token[129];
    if (admin) {
        char *body;
        esp_err_t result = read_body(req, 512, &body);
        if (!body) return result;
        if (!curio_json_valid(body, req->content_len)) { free(body); return error_response(req, "400 Bad Request", "invalid token request"); }
        cJSON *root = cJSON_Parse(body);
        free(body);
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "token");
        bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsString(value) && strlen(value->valuestring) <= 128;
        if (valid) snprintf(token, sizeof(token), "%s", value->valuestring);
        cJSON_Delete(root);
        if (!valid) return error_response(req, "400 Bad Request", "invalid token request");
    } else {
        uint8_t random[32];
        esp_fill_random(random, sizeof(random));
        for (size_t i = 0; i < sizeof(random); i++) snprintf(token + 2 * i, 3, "%02x", random[i]);
    }
    esp_err_t result = curio_settings_set_token(admin, token);
    return result == ESP_OK ? config_response(req) : save_error(req, result, "token must be 16-128 non-space ASCII characters");
}

static esp_err_t health_handler(httpd_req_t *req)
{
    return json_response(req, "200 OK", "{\"v\":2,\"status\":\"ok\",\"app\":\"curio\"}");
}

static esp_err_t diagnostics_handler(httpd_req_t *req)
{
    if (!authorized(req, true)) return require_auth(req);
    uint64_t now = curio_now_ms();
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    const curio_alert_notification_t *notification = curio_alerts_current(curio_app.alerts, now);
    unsigned alert_id = notification ? notification->id : 0;
    unsigned pending = (unsigned)curio_alerts_pending(curio_app.alerts);
    unsigned dropped = curio_alerts_dropped(curio_app.alerts);
    curio_battery_t battery = curio_app.battery;
    curio_orientation_status_t orientation = curio_app.orientation;
    bool auto_rotate = curio_app.device.auto_rotate;
    bool flash_enabled = curio_app.alert_config.flash_enabled;
    xSemaphoreGive(curio_app.mutex);
    curio_feedback_status_t feedback = curio_feedback_status();
    curio_output_status_t audio = feedback.audio, vibration = feedback.vibration, flash = feedback.flash;
    char json[1536], battery_json[320], orientation_json[192];
    curio_battery_json(&battery, now, battery_json, sizeof(battery_json));
    if (auto_rotate && orientation.active && orientation.valid &&
        now >= orientation.sampled_at_ms && now - orientation.sampled_at_ms <= 1500) {
        snprintf(orientation_json, sizeof(orientation_json),
            "{\"rotation\":%u,\"imu\":\"ok\",\"accel_g\":[%.3f,%.3f,%.3f],\"sample_age_ms\":%lu}",
            orientation.rotation, (double)orientation.x_g, (double)orientation.y_g,
            (double)orientation.z_g, (unsigned long)(now - orientation.sampled_at_ms));
    } else {
        snprintf(orientation_json, sizeof(orientation_json), "{\"rotation\":%u,\"imu\":\"%s\"}",
            orientation.rotation, auto_rotate ? "unavailable" : "disabled");
    }
    snprintf(json, sizeof(json), "{\"free_internal\":%u,\"min_internal\":%u,\"largest_internal\":%u,\"free_psram\":%u,\"tasks\":%u,\"widgets\":%u,\"alert_id\":%u,\"alerts_pending\":%u,\"alerts_dropped\":%u,\"audio_queued\":%u,\"audio_played\":%u,\"audio_failed\":%u,\"audio_dropped\":%u,\"audio_busy\":%s,\"audio_muted\":%s,\"vibration_queued\":%u,\"vibration_played\":%u,\"vibration_failed\":%u,\"vibration_dropped\":%u,\"vibration_busy\":%s,\"vibration_muted\":%s,\"flash_enabled\":%s,\"flash_queued\":%u,\"flash_played\":%u,\"flash_failed\":%u,\"flash_dropped\":%u,\"flash_busy\":%s,\"battery\":%s,\"orientation\":%s}",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)uxTaskGetNumberOfTasks(),
        (unsigned)curio_ui_widget_count(), alert_id, pending, dropped,
        (unsigned)audio.queued, (unsigned)audio.played, (unsigned)audio.failed,
        (unsigned)audio.dropped, audio.busy ? "true" : "false", audio.muted ? "true" : "false",
        (unsigned)vibration.queued, (unsigned)vibration.played, (unsigned)vibration.failed,
        (unsigned)vibration.dropped, vibration.busy ? "true" : "false", vibration.muted ? "true" : "false",
        flash_enabled ? "true" : "false", (unsigned)flash.queued, (unsigned)flash.played,
        (unsigned)flash.failed, (unsigned)flash.dropped, flash.busy ? "true" : "false", battery_json, orientation_json);
    return json_response(req, "200 OK", json);
}

static esp_err_t web_handler(httpd_req_t *req)
{
    const char *path = !strcmp(req->uri, "/") ? (setup_request(req) ? "/setup.html" : "/index.html") : req->uri;
    for (size_t i = 0; i < sizeof(curio_web_assets) / sizeof(curio_web_assets[0]); i++) {
        const curio_web_asset_t *asset = &curio_web_assets[i];
        if (strcmp(path, asset->path)) continue;
        httpd_resp_set_type(req, asset->type);
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; connect-src 'self' https://geocoding-api.open-meteo.com; script-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'");
        for (size_t offset = 0; offset < asset->size; offset += 2048) {
            size_t size = asset->size - offset;
            if (size > 2048) size = 2048;
            esp_err_t result = httpd_resp_send_chunk(req, (const char *)asset->data + offset, size);
            if (result != ESP_OK) return result;
        }
        return httpd_resp_send_chunk(req, NULL, 0);
    }
    return error_response(req, "404 Not Found", "not found");
}

static esp_err_t start_server(const curio_tls_config_t *tls)
{
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = (const uint8_t *)tls->certificate;
    config.servercert_len = strlen(tls->certificate) + 1;
    config.prvtkey_pem = (const uint8_t *)tls->private_key;
    config.prvtkey_len = strlen(tls->private_key) + 1;
    config.httpd.max_open_sockets = 6;
    config.httpd.lru_purge_enable = true;
    config.httpd.recv_wait_timeout = 5;
    config.httpd.send_wait_timeout = 5;
    config.httpd.stack_size = 10240;
    config.httpd.max_uri_handlers = 18;
    esp_err_t result = httpd_ssl_start(&server, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/index.html", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/style.css", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/wifi.js", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/api/v2/snapshot", .method = HTTP_POST, .handler = snapshot_handler},
        {.uri = "/api/v2/state", .method = HTTP_GET, .handler = state_handler},
        {.uri = "/api/v2/catalog", .method = HTTP_GET, .handler = state_handler},
        {.uri = "/vendor/js-yaml.min.js", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/api/v2/config", .method = HTTP_GET, .handler = config_handler},
        {.uri = "/api/v2/config", .method = HTTP_PUT, .handler = config_handler},
        {.uri = "/api/v2/tokens/admin", .method = HTTP_PUT, .handler = token_handler},
        {.uri = "/api/v2/tokens/collector", .method = HTTP_POST, .handler = token_handler},
        {.uri = "/api/v2/diagnostics", .method = HTTP_GET, .handler = diagnostics_handler},
        {.uri = "/healthz", .method = HTTP_GET, .handler = health_handler},
        {.uri = "/api/v2/restart", .method = HTTP_POST, .handler = restart_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) { httpd_ssl_stop(server); server = NULL; return result; }
    }
    return ESP_OK;
}

static esp_err_t start_setup(void)
{
    wifi_config_t ap = {.ap = {.channel = 1, .max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK}};
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Curio-%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%08u", (unsigned)(esp_random() % 100000000));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    esp_err_t result = esp_wifi_stop();
    if (result == ESP_OK) result = esp_wifi_set_mode(curio_app.configured ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (result == ESP_OK) result = esp_wifi_start();
    if (result != ESP_OK) return result;
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_app.network_error[0] = 0;
    strcpy(curio_app.setup_ssid, (char *)ap.ap.ssid);
    strcpy(curio_app.setup_password, (char *)ap.ap.password);
    xSemaphoreGive(curio_app.mutex);
    return ESP_OK;
}

static esp_err_t start_setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_netif_get_netif_impl_name(setup_interface, setup_interface_name.ifr_name));
    config.if_name = &setup_interface_name;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.stack_size = 12288;
    config.max_uri_handlers = 10;
    esp_err_t result = httpd_start(&setup_server, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/setup.html", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/setup.js", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/wifi.js", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/style.css", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/vendor/js-yaml.min.js", .method = HTTP_GET, .handler = web_handler},
        {.uri = "/api/v2/setup", .method = HTTP_GET, .handler = config_handler},
        {.uri = "/api/v2/setup", .method = HTTP_PUT, .handler = config_handler},
        {.uri = "/api/v2/setup/restart", .method = HTTP_POST, .handler = restart_handler},
        {.uri = "/api/v2/setup/reset", .method = HTTP_POST, .handler = reset_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        result = httpd_register_uri_handler(setup_server, &routes[i]);
        if (result != ESP_OK) { httpd_stop(setup_server); setup_server = NULL; return result; }
    }
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_app.setup_active = true;
    xSemaphoreGive(curio_app.mutex);
    ESP_LOGI(TAG, "Setup AP ready");
    return ESP_OK;
}

static void stop_setup(void)
{
    if (setup_server) { httpd_stop(setup_server); setup_server = NULL; }
    esp_wifi_set_mode(curio_app.configured ? WIFI_MODE_STA : WIFI_MODE_NULL);
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    curio_app.setup_active = false;
    curio_app.setup_password[0] = 0;
    xSemaphoreGive(curio_app.mutex);
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return;
        ip_event_got_ip_t *event = data;
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        curio_app.wifi_connected = true;
        snprintf(curio_app.ip, sizeof(curio_app.ip), IPSTR, IP2STR(&event->ip_info.ip));
        xSemaphoreGive(curio_app.mutex);
        xTaskNotify(network_task_handle, NETWORK_CONNECTED, eSetBits);
    } else if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)) {
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        curio_app.wifi_connected = false;
        curio_app.ip[0] = 0;
        xSemaphoreGive(curio_app.mutex);
        xTaskNotify(network_task_handle, NETWORK_DISCONNECTED, eSetBits);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        xTaskNotify(network_task_handle, NETWORK_SETUP_READY, eSetBits);
    }
}

static void network_task(void *arg)
{
    curio_network_config_t network;
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    network = curio_app.network;
    bool configured = curio_app.configured;
    xSemaphoreGive(curio_app.mutex);
    bool shared_trial = curio_app.import_shared_wifi && curio_wifi_load_shared(&network);
    curio_tls_config_t *tls = configured || shared_trial ? curio_alloc(sizeof(*tls)) : NULL;
    configASSERT(!(configured || shared_trial) || tls);
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    if (tls) *tls = curio_app.tls;
    xSemaphoreGive(curio_app.mutex);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *interface = esp_netif_create_default_wifi_sta();
    configASSERT(interface);
    setup_interface = esp_netif_create_default_wifi_ap();
    configASSERT(setup_interface);
    ESP_ERROR_CHECK(esp_netif_set_hostname(interface, network.hostname));
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(network.hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set("Curio status display"));
    ESP_ERROR_CHECK(mdns_service_add("Curio HTTPS", "_https", "_tcp", 443, NULL, 0));
    const esp_iris_mdns_config_t iris_discovery = {.instance_prefix = "Curio", .mode = "normal"};
    ESP_ERROR_CHECK(esp_iris_mdns_register(&iris_discovery));
    ESP_ERROR_CHECK(esp_wifi_start());
    curio_wifi_t connection = {.interface = interface};
    uint64_t service_revision = UINT64_MAX;
    if (!configured && !shared_trial) curio_network_setup(true);
    for (;;) {
        uint32_t events = 0;
        xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(1000));
        if (events & (NETWORK_SETUP_CHANGED | NETWORK_SETUP_READY)) {
            if (!atomic_load(&setup_requested)) stop_setup();
            else if (!setup_server) {
                esp_err_t result = ESP_OK;
                if (events & NETWORK_SETUP_CHANGED) result = start_setup();
                else if (esp_netif_is_netif_up(setup_interface)) result = start_setup_server();
                if (result != ESP_OK) {
                    atomic_store(&setup_requested, false);
                    stop_setup();
                    ESP_LOGE(TAG, "Setup failed: %s", esp_err_to_name(result));
                    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
                    snprintf(curio_app.network_error, sizeof(curio_app.network_error), "Setup: %s", esp_err_to_name(result));
                    xSemaphoreGive(curio_app.mutex);
                }
            }
        }
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        bool connected = curio_app.wifi_connected;
        bool clock_enabled = false;
        char ntp_server[128];
        strcpy(ntp_server, curio_app.device.ntp_server);
        clock_enabled = (curio_layout_services(&curio_app.layout) & CURIO_SERVICE_TIME) != 0;
        if (service_revision != curio_app.layout.revision) {
            for (size_t i = 0; i < curio_widget_service_count; i++)
                curio_widget_services[i]->configure(&curio_app.layout);
            service_revision = curio_app.layout.revision;
        }
        xSemaphoreGive(curio_app.mutex);
        if (shared_trial && atomic_load(&setup_requested)) {
            shared_trial = false;
            network.wifi_count = 0;
            esp_wifi_disconnect();
        }
        curio_wifi_update(&connection, &network, curio_now_ms(), connected, events & NETWORK_DISCONNECTED);
        if (shared_trial && connection.phase == CURIO_WIFI_ONLINE) {
            esp_err_t result = curio_settings_adopt_wifi(&network);
            if (result == ESP_OK) {
                xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
                *tls = curio_app.tls;
                curio_app.configured = true;
                xSemaphoreGive(curio_app.mutex);
                shared_trial = false;
                configured = true;
            } else {
                ESP_LOGE(TAG, "Shared Wi-Fi save failed: %s", esp_err_to_name(result));
                connection.phase = CURIO_WIFI_WAIT;
            }
        }
        if (shared_trial && connection.phase != CURIO_WIFI_CONNECTING && !configured) {
            shared_trial = false;
            network.wifi_count = 0;
            esp_wifi_disconnect();
            curio_network_setup(true);
        }
        if (!configured && !shared_trial) { free(tls); tls = NULL; }
        curio_clock_update(clock_enabled, connected, events & NETWORK_CONNECTED, ntp_server);
        if (configured && connected && !server && tls) {
            esp_err_t result = start_server(tls);
            if (result == ESP_OK) { free(tls); tls = NULL; }
            xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
            curio_app.https_ready = result == ESP_OK;
            xSemaphoreGive(curio_app.mutex);
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "HTTPS start failed: %s", esp_err_to_name(result));
            }
        }
        for (size_t i = 0; i < curio_widget_service_count; i++)
            curio_widget_services[i]->poll(curio_now_ms(), connected);
    }
}

void curio_network_start(void)
{
    atomic_store(&setup_requested, false);
    BaseType_t result = xTaskCreate(network_task, "curio_network", 6144, NULL, 5, &network_task_handle);
    configASSERT(result == pdPASS);
}

void curio_network_setup(bool enabled)
{
    atomic_store(&setup_requested, enabled);
    if (network_task_handle)
        xTaskNotify(network_task_handle, NETWORK_SETUP_CHANGED, eSetBits);
}
