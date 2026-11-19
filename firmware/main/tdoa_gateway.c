#include "tdoa_gateway.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "tdoa_radio.h"

enum {
    SYNC_INTERVAL_US = 200000, COLLECT_US = 80000, RETAIN_US = 1500000,
    UDP_PORT = 4777, UDP_BYTES = 25, OBSERVATIONS = 8, PENDING = 32, RETAINED = 64, PAGE = 8,
    JSON_BYTES = 1024,
};

typedef struct {
    tdoa_frame_t frame;
    uint16_t anchor;
    uint32_t anchor_boot;
} report_t;

typedef struct {
    uint64_t ticks;
    uint32_t boot;
    uint16_t anchor;
} observation_t;

typedef struct {
    tdoa_frame_t frame; // SYNC ticks come only from the main anchor's actual TX.
    int64_t opened_us;
    bool has_sync_tx;
    uint8_t rx_count;
    observation_t rx[OBSERVATIONS];
} event_t;

static const char *const TAG = "tdoa";
static QueueHandle_t reports;
static node_tdoa_config_t mode;
static uint32_t boot_id;
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;
static event_t pending[PENDING];
static size_t pending_head, pending_count;
static event_t history[RETAINED];
static uint32_t cursor;
static uint32_t gateway_boot;
static bool gateway_ready;

static bool same_frame(const tdoa_frame_t *a, const tdoa_frame_t *b)
{
    return a->kind == b->kind && a->source == b->source && a->boot == b->boot && a->sequence == b->sequence;
}

static void format_event(char json[JSON_BYTES], const event_t *event, uint32_t gateway)
{
    int pos = snprintf(json, JSON_BYTES, "{\"v\":1,\"type\":\"%s\",\"gateway_boot\":%" PRIu32 ",\"seq\":%" PRIu32,
                       event->frame.kind == TDOA_SYNC ? "sync" : "blink", gateway, event->frame.sequence);
    if (event->frame.kind == TDOA_SYNC) {
        pos += snprintf(json + pos, JSON_BYTES - pos, ",\"tx_ticks\":%" PRIu64, event->frame.ticks);
    } else {
        pos += snprintf(json + pos, JSON_BYTES - pos, ",\"tag\":%u,\"tag_boot\":%" PRIu32,
                        event->frame.source, event->frame.boot);
    }
    pos += snprintf(json + pos, JSON_BYTES - pos, ",\"rx\":[");
    for (size_t i = 0; i < event->rx_count; ++i) {
        const observation_t *rx = &event->rx[i];
        pos += snprintf(json + pos, JSON_BYTES - pos, "%s{\"anchor\":%u,\"boot\":%" PRIu32 ",\"ticks\":%" PRIu64 "}",
                        i ? "," : "", rx->anchor, rx->boot, rx->ticks);
    }
    snprintf(json + pos, JSON_BYTES - pos, "]}");
}

static void publish_oldest(void)
{
    const event_t *event = &pending[pending_head];
    pending_head = (pending_head + 1) % PENDING;
    --pending_count;
    if (event->frame.kind == TDOA_SYNC && !event->has_sync_tx) {
        return;
    }
    portENTER_CRITICAL(&event_lock);
    history[cursor++ % RETAINED] = *event;
    portEXIT_CRITICAL(&event_lock);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (usb_serial_jtag_is_connected()) {
        char json[JSON_BYTES];
        format_event(json, event, boot_id);
        printf("TDOA %s\n", json);
    }
#endif
}

static void merge_report(const report_t *report)
{
    const tdoa_frame_t *frame = &report->frame;
    if ((frame->kind == TDOA_SYNC && (frame->source != mode.id || frame->boot != boot_id)) ||
        (frame->kind == TDOA_BLINK && frame->source == mode.id)) {
        return;
    }
    event_t *event = NULL;
    for (size_t i = 0; i < pending_count; ++i) {
        event_t *candidate = &pending[(pending_head + i) % PENDING];
        if (same_frame(frame, &candidate->frame)) {
            event = candidate;
            break;
        }
    }
    if (event == NULL) {
        for (uint32_t i = 0; i < RETAINED && i < cursor; ++i) {
            if (same_frame(frame, &history[(cursor - 1 - i) % RETAINED].frame)) {
                return;
            }
        }
        if (pending_count == PENDING) {
            publish_oldest();
        }
        event = &pending[(pending_head + pending_count++) % PENDING];
        *event = (event_t){.frame = *frame, .opened_us = esp_timer_get_time()};
    }
    if (frame->kind == TDOA_SYNC && report->anchor == mode.id) {
        event->has_sync_tx = true;
        event->frame.ticks = frame->ticks;
        return;
    }
    for (size_t i = 0; i < event->rx_count; ++i) {
        if (event->rx[i].anchor == report->anchor) {
            return;
        }
    }
    if (event->rx_count < OBSERVATIONS) {
        event->rx[event->rx_count++] = (observation_t){.ticks = frame->ticks, .boot = report->anchor_boot,
                                                        .anchor = report->anchor};
    }
}

static void encode_report(const report_t *report, uint8_t bytes[UDP_BYTES])
{
    bytes[0] = 'U';
    bytes[1] = 'D';
    bytes[2] = 1;
    bytes[3] = report->frame.kind;
    tdoa_write_le(bytes + 4, report->anchor, 2);
    tdoa_write_le(bytes + 6, report->anchor_boot, 4);
    tdoa_write_le(bytes + 10, report->frame.source, 2);
    tdoa_write_le(bytes + 12, report->frame.boot, 4);
    tdoa_write_le(bytes + 16, report->frame.sequence, 4);
    tdoa_write_le(bytes + 20, report->frame.ticks, 5);
}

static void receive_reports(int socket_fd)
{
    for (size_t i = 0; i < 32; ++i) {
        uint8_t bytes[UDP_BYTES + 1];
        int size = recvfrom(socket_fd, bytes, sizeof(bytes), MSG_DONTWAIT, NULL, NULL);
        if (size < 0) {
            return;
        }
        if (size != UDP_BYTES || bytes[0] != 'U' || bytes[1] != 'D' || bytes[2] != 1 ||
            (bytes[3] != TDOA_SYNC && bytes[3] != TDOA_BLINK)) {
            continue;
        }
        report_t report = {.anchor = tdoa_read_le(bytes + 4, 2), .anchor_boot = tdoa_read_le(bytes + 6, 4),
                           .frame = {.kind = bytes[3], .source = tdoa_read_le(bytes + 10, 2),
                                     .boot = tdoa_read_le(bytes + 12, 4), .sequence = tdoa_read_le(bytes + 16, 4),
                                     .ticks = tdoa_read_le(bytes + 20, 5)}};
        if (report.anchor != mode.id && report.anchor != 0 && report.anchor != UINT16_MAX &&
            report.frame.source != 0 && report.frame.source != UINT16_MAX && report.frame.source != report.anchor) {
            merge_report(&report);
        }
    }
}

static void transport_task(void *arg)
{
    (void)arg;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const struct sockaddr_in destination = {.sin_family = AF_INET, .sin_port = htons(UDP_PORT),
                                            .sin_addr.s_addr = inet_addr("192.168.4.1")};
    const struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(UDP_PORT),
                                      .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (socket_fd < 0 || (mode.role == TDOA_MAIN &&
        bind(socket_fd, (const struct sockaddr *)&local, sizeof(local)) != 0)) {
        ESP_LOGE(TAG, "Could not open anchor UDP socket");
        if (socket_fd >= 0) {
            close(socket_fd);
        }
        vTaskDelete(NULL);
        return;
    }
    if (mode.role == TDOA_MAIN) {
        portENTER_CRITICAL(&event_lock);
        gateway_boot = boot_id;
        gateway_ready = true;
        portEXIT_CRITICAL(&event_lock);
    }
    while (true) {
        report_t report;
        if (xQueueReceive(reports, &report, 1) == pdTRUE) {
            if (mode.role == TDOA_MAIN) {
                merge_report(&report);
            } else {
                uint8_t bytes[UDP_BYTES];
                encode_report(&report, bytes);
                sendto(socket_fd, bytes, sizeof(bytes), MSG_DONTWAIT,
                       (const struct sockaddr *)&destination, sizeof(destination));
            }
        }
        if (mode.role == TDOA_MAIN) {
            receive_reports(socket_fd);
            int64_t now = esp_timer_get_time();
            while (pending_count && now - pending[pending_head].opened_us >= COLLECT_US) {
                publish_oldest();
            }
        }
    }
}

static void queue_frame(const tdoa_frame_t *frame)
{
    report_t report = {.frame = *frame, .anchor = mode.id, .anchor_boot = boot_id};
    xQueueSend(reports, &report, 0);
}

void tdoa_run(node_tdoa_config_t config)
{
    mode = config;
    boot_id = esp_random();
    bool tag = config.role == TDOA_TAG;
    bool main = config.role == TDOA_MAIN;
    if (!tag) {
        reports = xQueueCreate(32, sizeof(report_t));
        if (reports == NULL || xTaskCreate(transport_task, "tdoa_transport", 4608, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Could not start TDoA transport task");
            if (reports != NULL) {
                vQueueDelete(reports);
            }
            return;
        }
    }
    ESP_LOGI(TAG, "TDoA %s node %u", tag ? "tag" : main ? "main anchor" : "secondary anchor", config.id);
    uint32_t sequence = 0;
    int64_t next = 0;
    bool listening = false;
    while (true) {
        tdoa_frame_t frame;
        if (!tag && tdoa_radio_receive(&frame, &listening) && frame.source != config.id &&
            (frame.kind == TDOA_BLINK || (!main && frame.source == config.main_id))) {
            queue_frame(&frame);
        }
        const int64_t now = esp_timer_get_time();
        if ((tag || main) && now >= next) {
            frame = (tdoa_frame_t){.source = config.id, .kind = tag ? TDOA_BLINK : TDOA_SYNC,
                                   .boot = boot_id, .sequence = sequence++};
            bool sent = tdoa_radio_transmit(&frame);
            next = now + (tag ? 80000 + esp_random() % 40001 : SYNC_INTERVAL_US);
            listening = main && sent;
            if (listening) {
                queue_frame(&frame);
            }
        }
        vTaskDelay(1);
    }
}

static bool query_value(const char *query, const char *key, uint32_t *value)
{
    char text[12];
    esp_err_t status = httpd_query_key_value(query, key, text, sizeof(text));
    if (status == ESP_ERR_NOT_FOUND) {
        return true;
    }
    if (status != ESP_OK || text[0] == '\0') {
        return false;
    }
    uint64_t number = 0;
    for (const char *c = text; *c; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        number = number * 10 + (*c - '0');
    }
    if (number > UINT32_MAX) {
        return false;
    }
    *value = number;
    return true;
}

esp_err_t tdoa_serve_events(httpd_req_t *request)
{
    if (node_dashboard_get_tdoa_config().role != TDOA_MAIN) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Select TDoA Main anchor on this board.");
    }
    char query[80];
    uint32_t after = 0, requested_boot = 0;
    if (httpd_req_get_url_query_len(request) &&
        (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
         !query_value(query, "after", &after) || !query_value(query, "boot", &requested_boot))) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid TDoA cursor.");
    }
    event_t page[PAGE];
    size_t size = 0;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&event_lock);
    bool ready = gateway_ready;
    uint32_t boot = gateway_boot;
    uint32_t newest = cursor;
    uint32_t last = requested_boot == boot && after <= newest ? after : 0;
    uint32_t oldest = newest > RETAINED ? newest - RETAINED : 0;
    if (last < oldest) {
        last = oldest;
    }
    while (last < newest && size < PAGE) {
        const event_t *event = &history[last++ % RETAINED];
        if (now - event->opened_us <= RETAIN_US) {
            page[size++] = *event;
        }
    }
    portEXIT_CRITICAL(&event_lock);
    if (!ready) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "TDoA gateway is starting.");
    }
    if (requested_boot == boot && after > newest) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Cursor is ahead of the TDoA stream.");
    }
    char json[JSON_BYTES];
    snprintf(json, sizeof(json), "{\"v\":1,\"gateway_boot\":%" PRIu32 ",\"cursor\":%" PRIu32 ",\"events\":[", boot, last);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t status = httpd_resp_sendstr_chunk(request, json);
    for (size_t i = 0; i < size && status == ESP_OK; ++i) {
        if (i && httpd_resp_sendstr_chunk(request, ",") != ESP_OK) {
            return ESP_FAIL;
        }
        format_event(json, &page[i], boot);
        status = httpd_resp_sendstr_chunk(request, json);
    }
    if (status == ESP_OK) {
        status = httpd_resp_sendstr_chunk(request, "]}");
    }
    return status == ESP_OK ? httpd_resp_sendstr_chunk(request, NULL) : status;
}
