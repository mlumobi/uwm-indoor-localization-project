#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
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
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "node_dashboard.h"
#include "ranging_log.h"
#include "tdoa_gateway.h"
#include "uwb_ranging.h"

#define SETTINGS_KEY "settings_v2"
#define LEGACY_SETTINGS_KEY "settings_v1"
#define SETUP_PASSWORD "88888888"
#define SETTINGS_CONTENT_TYPE "application/vnd.uwm.settings"

enum {
    LABEL_MAX_LENGTH = 32,
    NODE_ID_MAX = 65534,
    ANCHOR_ROLE_MARKER = UINT16_MAX,
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
    uint16_t target_anchor_id;
    uint8_t tdoa_role;
    uint16_t tdoa_main_id;
    char tdoa_main_ssid[16];
} node_settings_t;

// The original settings blob ends immediately before target_anchor_id.
_Static_assert(offsetof(node_settings_t, target_anchor_id) == 36, "Legacy settings layout changed");
_Static_assert(offsetof(node_settings_t, tdoa_role) == 38, "TWR settings layout changed");

static const char *const TAG = "dashboard";
static nvs_handle_t storage;
// The HTTP server serializes reads and writes after startup.
static node_settings_t settings;
static char mac_address[18];
static char setup_ssid[16];
static atomic_uint_least32_t radio_id;
static atomic_uint_least32_t ranging_config;
static node_tdoa_config_t boot_tdoa_config;
static esp_timer_handle_t restart_timer;

extern const char dashboard_html_start[] asm("_binary_dashboard_html_start");

void node_dashboard_set_radio_id(uint32_t id)
{
    atomic_store(&radio_id, id);
}

static void update_ranging_config(void)
{
    uint32_t target = settings.role == ROLE_ANCHOR ? ANCHOR_ROLE_MARKER : settings.target_anchor_id;
    atomic_store(&ranging_config, (target << 16) | settings.id);
}

node_ranging_config_t node_dashboard_get_ranging_config(void)
{
    uint32_t config = atomic_load(&ranging_config);
    uint16_t target = config >> 16;
    bool anchor = target == ANCHOR_ROLE_MARKER;
    return (node_ranging_config_t){ .id = (uint16_t)config,
                                    .target_anchor_id = anchor ? 0 : target, .anchor = anchor };
}

node_tdoa_config_t node_dashboard_get_tdoa_config(void)
{
    return boot_tdoa_config;
}

static bool valid_gateway_ssid(const char *ssid)
{
    if (strnlen(ssid, sizeof(settings.tdoa_main_ssid)) != 10 || strncmp(ssid, "UWM-", 4) != 0) {
        return false;
    }
    for (size_t i = 4; i < 10; ++i) {
        if (!((ssid[i] >= '0' && ssid[i] <= '9') || (ssid[i] >= 'A' && ssid[i] <= 'F'))) {
            return false;
        }
    }
    return strcmp(ssid, setup_ssid) != 0;
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
    node_settings_t saved = {0};
    size_t size = sizeof(saved);
    esp_err_t result = nvs_get_blob(storage, SETTINGS_KEY, &saved, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        size = sizeof(saved);
        result = nvs_get_blob(storage, LEGACY_SETTINGS_KEY, &saved, &size);
    }
    if (result == ESP_OK && (size == sizeof(saved) || size == offsetof(node_settings_t, tdoa_role) ||
        size == offsetof(node_settings_t, target_anchor_id)) &&
        saved.id > 0 &&
        saved.id <= NODE_ID_MAX && saved.role <= ROLE_ANCHOR && valid_label(saved.label)) {
        if (saved.role == ROLE_ANCHOR || saved.target_anchor_id > NODE_ID_MAX ||
            saved.target_anchor_id == saved.id) {
            saved.target_anchor_id = 0;
        }
        if (saved.tdoa_role > TDOA_MAIN || (saved.tdoa_role == TDOA_SECONDARY &&
            (saved.tdoa_main_id == 0 || saved.tdoa_main_id > NODE_ID_MAX || saved.tdoa_main_id == saved.id ||
             !valid_gateway_ssid(saved.tdoa_main_ssid)))) {
            saved.tdoa_role = TDOA_OFF;
        }
        if (saved.tdoa_role != TDOA_SECONDARY) {
            saved.tdoa_main_id = 0;
            saved.tdoa_main_ssid[0] = '\0';
        }
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
    const char *twr_role = settings.role == ROLE_ANCHOR ? "anchor" : "tag";
    const char *role = settings.tdoa_role == TDOA_MAIN ? "main" : settings.tdoa_role == TDOA_SECONDARY ? "anchor" :
                       settings.tdoa_role == TDOA_TAG ? "tag" : twr_role;
    wifi_ap_record_t upstream;
    bool joined = boot_tdoa_config.role == TDOA_SECONDARY && esp_wifi_sta_get_ap_info(&upstream) == ESP_OK;
    char json[420];
    snprintf(json, sizeof(json),
             "{\"label\":\"%s\",\"role\":\"%s\",\"id\":%u,\"targetAnchorId\":%u,"
             "\"mode\":\"%s\",\"twrRole\":\"%s\",\"mainAnchorId\":%u,\"mainAnchorSsid\":\"%s\",\"mainAnchorConnected\":%s,\"restarting\":%s,"
             "\"mac\":\"%s\",\"ssid\":\"%s\",\"radioId\":\"0x%08" PRIX32 "\"}",
             settings.label, role, settings.id, settings.target_anchor_id, settings.tdoa_role == TDOA_OFF ? "twr" : "tdoa",
             twr_role, settings.tdoa_main_id, settings.tdoa_main_ssid, joined ? "true" : "false",
             esp_timer_is_active(restart_timer) ? "true" : "false", mac_address, setup_ssid, (uint32_t)atomic_load(&radio_id));
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t serve_twr(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (settings.tdoa_role != TDOA_OFF) {
        return httpd_resp_sendstr(request, "{\"role\":\"tdoa\",\"sample\":null}");
    }
    if (settings.role == ROLE_ANCHOR) {
        return httpd_resp_sendstr(request, "{\"role\":\"anchor\",\"sample\":null}");
    }

    uwb_twr_sample_t sample;
    if (!uwb_ranging_get_latest(&sample) || sample.tag_id != settings.id ||
        (settings.target_anchor_id != 0 && sample.anchor_id != settings.target_anchor_id)) {
        return httpd_resp_sendstr(request, "{\"role\":\"tag\",\"sample\":null}");
    }

    int64_t age_ms = (esp_timer_get_time() - sample.completed_at_us) / 1000;
    char json[256];
    snprintf(json, sizeof(json),
             "{\"role\":\"tag\",\"sample\":{\"tagId\":%u,\"anchorId\":%u,\"sequence\":%u,"
             "\"roundATicks\":%" PRIu32 ",\"replyATicks\":%" PRIu32 ","
             "\"roundBTicks\":%" PRIu32 ",\"replyBTicks\":%" PRIu32 ",\"ageMs\":%" PRId64 "}}",
             sample.tag_id, sample.anchor_id, sample.sequence,
             sample.round_a_ticks, sample.reply_a_ticks, sample.round_b_ticks, sample.reply_b_ticks,
             age_ms);
    return httpd_resp_sendstr(request, json);
}

static esp_err_t serve_anchor_ranges(httpd_req_t *request)
{
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (settings.tdoa_role != TDOA_OFF) {
        return httpd_resp_sendstr(request, "{\"role\":\"tdoa\"}");
    }
    if (settings.role != ROLE_ANCHOR) {
        return httpd_resp_sendstr(request, "{\"role\":\"tag\"}");
    }

    uwb_anchor_sample_t samples[UWB_MAX_ANCHOR_TAGS];
    size_t count = uwb_ranging_get_anchor_samples(settings.id, samples);
    const int64_t now = esp_timer_get_time();
    char json[200];
    snprintf(json, sizeof(json), "{\"role\":\"anchor\",\"anchorId\":%u,\"tags\":[", settings.id);
    esp_err_t result = httpd_resp_sendstr_chunk(request, json);
    for (size_t i = 0; i < count && result == ESP_OK; ++i) {
        const uwb_anchor_sample_t *sample = &samples[i];
        snprintf(json, sizeof(json), "%s{\"tagId\":%u,\"sequence\":%u,\"distanceMm\":%" PRId32
                 ",\"bootId\":%" PRIu32 ",\"recordId\":%" PRIu32 ",\"ageMs\":%" PRId64 "}",
                 i == 0 ? "" : ",", sample->tag_id, sample->sequence, sample->distance_mm,
                 sample->boot_id, sample->record_id, (now - sample->completed_at_us) / 1000);
        result = httpd_resp_sendstr_chunk(request, json);
    }
    if (result == ESP_OK) {
        result = httpd_resp_sendstr_chunk(request, "]}");
    }
    return result == ESP_OK ? httpd_resp_sendstr_chunk(request, NULL) : result;
}

static bool parse_node_id(const char *text, uint16_t *id)
{
    uint32_t value = 0;
    for (const char *digit = text; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
        value = value * 10 + (*digit - '0');
        if (value > NODE_ID_MAX) {
            return false;
        }
    }
    *id = value;
    return value != 0;
}

static bool parse_settings(char *body, node_settings_t *parsed)
{
    // TWR keeps its original four fields. TDoA adds the main anchor's SSID for receivers.
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
    char *target = strchr(label, '\n');
    char *ssid = NULL;
    if (target != NULL) {
        *target++ = '\0';
        ssid = strchr(target, '\n');
        if (ssid != NULL) {
            *ssid++ = '\0';
        }
    }

    if (strcmp(body, "tag") == 0) {
        parsed->role = ROLE_TAG;
    } else if (strcmp(body, "anchor") == 0) {
        parsed->role = ROLE_ANCHOR;
    } else if (strcmp(body, "tdoa-tag") == 0) {
        parsed->tdoa_role = TDOA_TAG;
    } else if (strcmp(body, "tdoa-anchor") == 0) {
        parsed->tdoa_role = TDOA_SECONDARY;
    } else if (strcmp(body, "tdoa-main") == 0) {
        parsed->tdoa_role = TDOA_MAIN;
    } else {
        return false;
    }

    if (!parse_node_id(id, &parsed->id) || !valid_label(label)) {
        return false;
    }
    if (parsed->tdoa_role != TDOA_OFF) {
        parsed->role = settings.role;
        parsed->target_anchor_id = settings.target_anchor_id != parsed->id ? settings.target_anchor_id : 0;
        if (parsed->tdoa_role == TDOA_SECONDARY) {
            if (target == NULL || !parse_node_id(target, &parsed->tdoa_main_id) || parsed->tdoa_main_id == parsed->id ||
                ssid == NULL || !valid_gateway_ssid(ssid)) {
                return false;
            }
            strcpy(parsed->tdoa_main_ssid, ssid);
        } else if ((target != NULL && *target) || (ssid != NULL && *ssid)) {
            return false;
        }
    } else {
        if (ssid != NULL) {
            return false;
        }
        if (target == NULL && parsed->role == ROLE_TAG) {
            parsed->target_anchor_id = settings.target_anchor_id;
        } else if (target != NULL && *target != '\0' && !parse_node_id(target, &parsed->target_anchor_id)) {
            return false;
        }
        if (parsed->target_anchor_id == parsed->id || (parsed->role == ROLE_ANCHOR && parsed->target_anchor_id != 0)) {
            return false;
        }
    }
    strcpy(parsed->label, label);
    return true;
}

static esp_err_t save_settings(httpd_req_t *request)
{
    if (esp_timer_is_active(restart_timer)) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "The board is restarting.");
    }
    char content_type[sizeof(SETTINGS_CONTENT_TYPE)];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type,
                                   sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, SETTINGS_CONTENT_TYPE) != 0) {
        httpd_resp_set_status(request, "415 Unsupported Media Type");
        return httpd_resp_sendstr(request, "Use the dashboard to submit settings.");
    }

    char body[96];
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
                                  "Check the role, node ID, label, and anchor settings.");
    }

    bool tdoa_changed = settings.tdoa_role != updated.tdoa_role || settings.tdoa_main_id != updated.tdoa_main_id ||
                        strcmp(settings.tdoa_main_ssid, updated.tdoa_main_ssid) != 0;
    bool needs_restart = tdoa_changed || (updated.tdoa_role != TDOA_OFF && settings.id != updated.id);
    if (!tdoa_changed && settings.role == updated.role && settings.id == updated.id &&
        settings.target_anchor_id == updated.target_anchor_id &&
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
    if (needs_restart) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(restart_timer, 750000));
    } else if (settings.tdoa_role == TDOA_OFF) {
        update_ranging_config();
        ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(MODE_LED_GPIO, settings.role == ROLE_ANCHOR));
    }
    ESP_LOGI(TAG, "Saved %s node %u (%s)", settings.role == ROLE_ANCHOR ? "anchor" : "tag",
             settings.id, settings.label);
    return serve_settings(request);
}

static void restart_board(void *arg)
{
    (void)arg;
    esp_restart();
}

static void connect_to_main(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        static uint8_t last_reason;
        if (last_reason != disconnected->reason) {
            last_reason = disconnected->reason;
            ESP_LOGW(TAG, "Main anchor Wi-Fi disconnected (reason %u)", last_reason);
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = data;
        ESP_LOGI(TAG, "Main anchor Wi-Fi link up at " IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
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
    boot_tdoa_config = (node_tdoa_config_t){.role = settings.tdoa_role, .id = settings.id, .main_id = settings.tdoa_main_id};
    update_ranging_config();
    const esp_timer_create_args_t restart = {.callback = restart_board, .name = "settings_restart"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&restart, &restart_timer), TAG, "create settings timer");

    bool secondary = settings.tdoa_role == TDOA_SECONDARY;
    bool anchor = secondary || settings.tdoa_role == TDOA_MAIN ||
                  (settings.tdoa_role == TDOA_OFF && settings.role == ROLE_ANCHOR);
    ESP_RETURN_ON_ERROR(gpio_set_level(MODE_LED_GPIO, anchor), TAG, "set role");
    ESP_RETURN_ON_ERROR(gpio_set_direction(MODE_LED_GPIO, GPIO_MODE_OUTPUT), TAG, "enable role LED");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize networking");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop");
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(ap_netif != NULL, ESP_ERR_NO_MEM, TAG, "create access point interface");
    if (secondary) {
        // Keep the setup AP off the subnet used by the main anchor's AP.
        const esp_netif_ip_info_t setup_ip = {.ip.addr = ESP_IP4TOADDR(192, 168, 5, 1),
                                              .gw.addr = ESP_IP4TOADDR(192, 168, 5, 1),
                                              .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0)};
        ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(ap_netif), TAG, "stop setup DHCP server");
        ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(ap_netif, &setup_ip), TAG, "set setup AP address");
        ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(ap_netif), TAG, "start setup DHCP server");
        ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_ERR_NO_MEM, TAG, "create anchor station");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_to_main, NULL),
                            TAG, "register station event handler");
        ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_to_main, NULL),
                            TAG, "register station address handler");
    }
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init), TAG, "initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "select Wi-Fi settings storage");
    wifi_config_t wifi = {
        .ap = {
            .channel = 1,
            .password = SETUP_PASSWORD,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = settings.tdoa_role == TDOA_MAIN ? 6 : 2,
        },
    };
    memcpy(wifi.ap.ssid, setup_ssid, strlen(setup_ssid));
    wifi.ap.ssid_len = strlen(setup_ssid);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(secondary ? WIFI_MODE_APSTA : WIFI_MODE_AP), TAG, "select Wi-Fi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi), TAG, "configure access point");
    if (secondary) {
        wifi_config_t sta = {.sta = {.password = SETUP_PASSWORD, .channel = 1, .threshold.authmode = WIFI_AUTH_WPA2_PSK}};
        memcpy(sta.sta.ssid, settings.tdoa_main_ssid, strlen(settings.tdoa_main_ssid));
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "configure the main anchor network");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
    if (secondary) {
        ESP_LOGI(TAG, "Joining main anchor %u on %s", settings.tdoa_main_id, settings.tdoa_main_ssid);
        ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect to main anchor");
    }

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.lru_purge_enable = true;
    http.stack_size = 8192; // CSV export and TDoA event pages are formatted on the server task.
    httpd_handle_t server;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &http), TAG, "start HTTP server");
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = serve_dashboard },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = serve_settings },
        { .uri = "/api/settings", .method = HTTP_PUT, .handler = save_settings },
        { .uri = "/api/twr", .method = HTTP_GET, .handler = serve_twr },
        { .uri = "/api/twr/tags", .method = HTTP_GET, .handler = serve_anchor_ranges },
        { .uri = "/api/twr/log.csv", .method = HTTP_GET, .handler = ranging_log_download },
        { .uri = "/api/tdoa/events", .method = HTTP_GET, .handler = tdoa_serve_events },
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
    ESP_LOGI(TAG, "Loaded %s node %u (%s)", settings.tdoa_role == TDOA_OFF ? (anchor ? "anchor" : "tag") :
             (secondary ? "TDoA secondary anchor" : anchor ? "TDoA main anchor" : "TDoA tag"), settings.id, settings.label);
    return ESP_OK;
}
