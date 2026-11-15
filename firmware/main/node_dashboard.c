#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "node_dashboard.h"

#define SETTINGS_KEY "settings_v1"
#define SETUP_PASSWORD "88888888"
#define SETTINGS_CONTENT_TYPE "application/vnd.uwm.settings"

enum {
    LABEL_MAX_LENGTH = 32,
    NODE_ID_MAX = 65534,
    MODE_LED_GPIO = GPIO_NUM_21,
};

typedef enum {
    ROLE_TAG,
    ROLE_ANCHOR,
} node_role_t;

typedef struct {
    uint16_t id;
    uint8_t role;
    char label[LABEL_MAX_LENGTH + 1];
} node_settings_t;

static const char *const TAG = "dashboard";
static nvs_handle_t storage;
// The HTTP server serializes reads and writes after startup.
static node_settings_t settings;
static char mac_address[18];
static char setup_ssid[16];
static atomic_uint_least32_t radio_id;
static atomic_uint_least32_t ranging_config;

extern const char dashboard_html_start[] asm("_binary_dashboard_html_start");

void node_dashboard_set_radio_id(uint32_t id)
{
    atomic_store(&radio_id, id);
}

static void update_ranging_config(void)
{
    atomic_store(&ranging_config, ((uint32_t)settings.id << 1) | (settings.role == ROLE_ANCHOR));
}

void node_dashboard_get_ranging_config(uint16_t *id, bool *anchor)
{
    uint32_t config = atomic_load(&ranging_config);
    *id = config >> 1;
    *anchor = (config & 1) != 0;
}

static bool valid_label(const char *label)
{
    size_t length = strnlen(label, LABEL_MAX_LENGTH + 1);
    if (length == 0 || length > LABEL_MAX_LENGTH) {
        return false;
    }

    bool has_letter_or_digit = false;
    for (size_t i = 0; i < length; i++) {
        char c = label[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            has_letter_or_digit = true;
        } else if (c != ' ' && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return has_letter_or_digit;
}

static esp_err_t load_settings(uint16_t suggested_id)
{
    node_settings_t saved;
    size_t size = sizeof(saved);
    esp_err_t result = nvs_get_blob(storage, SETTINGS_KEY, &saved, &size);
    if (result == ESP_OK && size == sizeof(saved) && saved.id > 0 &&
        saved.id <= NODE_ID_MAX && saved.role <= ROLE_ANCHOR && valid_label(saved.label)) {
        settings = saved;
        return ESP_OK;
    }
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND &&
        result != ESP_ERR_NVS_INVALID_LENGTH && result != ESP_ERR_NVS_TYPE_MISMATCH) {
        return result;
    }

    settings.id = suggested_id;
    settings.role = ROLE_TAG;
    snprintf(settings.label, sizeof(settings.label), "%s", setup_ssid);
    if (result != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Stored node settings are invalid. Using defaults.");
    }
    return ESP_OK;
}

static esp_err_t serve_dashboard(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, dashboard_html_start);
}

static esp_err_t serve_settings(httpd_req_t *request)
{
    char json[240];
    snprintf(json, sizeof(json),
             "{\"label\":\"%s\",\"role\":\"%s\",\"id\":%u,"
             "\"mac\":\"%s\",\"ssid\":\"%s\",\"radioId\":\"0x%08" PRIX32 "\"}",
             settings.label, settings.role == ROLE_ANCHOR ? "anchor" : "tag",
             settings.id, mac_address, setup_ssid, (uint32_t)atomic_load(&radio_id));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json);
}

static bool parse_settings(char *body, node_settings_t *parsed)
{
    // The dashboard sends three LF-separated fields: role, decimal ID, label.
    char *id = strchr(body, '\n');
    if (id == NULL) {
        return false;
    }
    *id++ = '\0';
    char *label = strchr(id, '\n');
    if (label == NULL) {
        return false;
    }
    *label++ = '\0';

    if (strcmp(body, "tag") == 0) {
        parsed->role = ROLE_TAG;
    } else if (strcmp(body, "anchor") == 0) {
        parsed->role = ROLE_ANCHOR;
    } else {
        return false;
    }

    if (*id == '\0' || !valid_label(label)) {
        return false;
    }
    uint32_t value = 0;
    for (const char *digit = id; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        value = value * 10 + (*digit - '0');
        if (value > NODE_ID_MAX) {
            return false;
        }
    }
    if (value == 0) {
        return false;
    }
    parsed->id = value;
    strcpy(parsed->label, label);
    return true;
}

static esp_err_t save_settings(httpd_req_t *request)
{
    char content_type[sizeof(SETTINGS_CONTENT_TYPE)];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
                                   sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, SETTINGS_CONTENT_TYPE) != 0) {
        httpd_resp_set_status(request, "415 Unsupported Media Type");
        return httpd_resp_sendstr(request, "Use the dashboard to submit settings.");
    }

    char body[64];
    if (request->content_len == 0 || request->content_len >= sizeof(body)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid settings length.");
    }
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            return ESP_FAIL;
        }
        received += count;
    }
    body[received] = '\0';

    node_settings_t updated = {0};
    if (memchr(body, '\0', received) != NULL || !parse_settings(body, &updated)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                  "Check the role, node ID, and label.");
    }

    if (settings.role == updated.role && settings.id == updated.id &&
        strcmp(settings.label, updated.label) == 0) {
        return serve_settings(request);
    }
    esp_err_t result = nvs_set_blob(storage, SETTINGS_KEY, &updated, sizeof(updated));
    if (result == ESP_OK) {
        result = nvs_commit(storage);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Could not save node settings: %s", esp_err_to_name(result));
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                  "Could not save settings to flash.");
    }

    settings = updated;
    update_ranging_config();
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(MODE_LED_GPIO, settings.role == ROLE_ANCHOR));
    ESP_LOGI(TAG, "Saved %s node %u (%s)", settings.role == ROLE_ANCHOR ? "anchor" : "tag",
             settings.id, settings.label);
    return serve_settings(request);
}

esp_err_t node_dashboard_start(void)
{
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read MAC");
    snprintf(mac_address, sizeof(mac_address), MACSTR, MAC2STR(mac));
    snprintf(setup_ssid, sizeof(setup_ssid), "UWM-%02X%02X%02X", mac[3], mac[4], mac[5]);
    uint16_t suggested_id = ((uint16_t)mac[4] << 8) | mac[5];
    if (suggested_id == 0 || suggested_id > NODE_ID_MAX) {
        suggested_id = 1;
    }

    // Preserve flash contents if NVS initialization needs attention.
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "initialize NVS");
    ESP_RETURN_ON_ERROR(nvs_open("uwm_node", NVS_READWRITE, &storage), TAG, "open node settings");
    ESP_RETURN_ON_ERROR(load_settings(suggested_id), TAG, "load node settings");
    update_ranging_config();

    ESP_RETURN_ON_ERROR(gpio_set_level(MODE_LED_GPIO, settings.role == ROLE_ANCHOR), TAG, "set role");
    ESP_RETURN_ON_ERROR(gpio_set_direction(MODE_LED_GPIO, GPIO_MODE_OUTPUT), TAG, "enable role LED");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize networking");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop");
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(ap_netif != NULL, ESP_ERR_NO_MEM, TAG, "create access point interface");
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), TAG, "initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "select Wi-Fi settings storage");
    wifi_config_t wifi = {
        .ap = {
            .channel = 1,
            .password = SETUP_PASSWORD,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 2,
        },
    };
    memcpy(wifi.ap.ssid, setup_ssid, strlen(setup_ssid));
    wifi.ap.ssid_len = strlen(setup_ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "select access point mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi), TAG, "configure access point");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.lru_purge_enable = true;
    httpd_handle_t server;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &http), TAG, "start HTTP server");
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = serve_dashboard },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = serve_settings },
        { .uri = "/api/settings", .method = HTTP_PUT, .handler = save_settings },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        esp_err_t result = httpd_register_uri_handler(server, &routes[i]);
        if (result != ESP_OK) {
            httpd_stop(server);
            return result;
        }
    }

    esp_netif_ip_info_t ip;
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(ap_netif, &ip), TAG, "get dashboard address");
    ESP_LOGI(TAG, "Setup Wi-Fi: %s | Password: %s | http://" IPSTR, setup_ssid, SETUP_PASSWORD, IP2STR(&ip.ip));
    ESP_LOGI(TAG, "Loaded %s node %u (%s)", settings.role == ROLE_ANCHOR ? "anchor" : "tag",
             settings.id, settings.label);
    return ESP_OK;
}
