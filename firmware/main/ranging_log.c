#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ranging_log.h"

#define LOG_PARTITION "twr_log"
#define LOG_NAMESPACE "twr_v1"

enum {
    LOG_CAPACITY = 2048,
    QUEUE_LENGTH = 16,
};

typedef struct {
    uint32_t number;
    uint32_t boot_id;
    int64_t uptime_ms;
    ranging_log_measurement_t measurement;
} log_record_t;

static const char *const TAG = "twr_log";
static nvs_handle_t storage;
static QueueHandle_t pending_records;
static atomic_uint_least32_t latest_number;
static uint32_t boot_id;

static void slot_key(char key[8], unsigned slot)
{
    snprintf(key, 8, "r%04u", slot);
}

static esp_err_t read_slot(unsigned slot, log_record_t *record)
{
    char key[8];
    slot_key(key, slot);
    size_t size = sizeof(*record);
    esp_err_t result = nvs_get_blob(storage, key, record, &size);
    if (result == ESP_OK && size != sizeof(*record)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return result;
}

static void log_task(void *arg)
{
    (void)arg;
    log_record_t record;
    while (true) {
        if (xQueueReceive(pending_records, &record, portMAX_DELAY) != pdPASS) {
            continue;
        }
        record.number = atomic_load(&latest_number) + 1;
        record.boot_id = boot_id;
        char key[8];
        slot_key(key, (record.number - 1) % LOG_CAPACITY);
        esp_err_t result = nvs_set_blob(storage, key, &record, sizeof(record));
        if (result == ESP_OK) {
            result = nvs_commit(storage);
        }
        if (result == ESP_OK) {
            atomic_store(&latest_number, record.number);
        } else {
            ESP_LOGE(TAG, "Could not save measurement: %s", esp_err_to_name(result));
        }
    }
}

esp_err_t ranging_log_start(void)
{
    ESP_RETURN_ON_ERROR(nvs_flash_init_partition(LOG_PARTITION), TAG, "initialize log partition");
    ESP_RETURN_ON_ERROR(nvs_open_from_partition(LOG_PARTITION, LOG_NAMESPACE, NVS_READWRITE,
                                                &storage), TAG, "open log storage");

    // The record numbers recover the write position without a separate flash update.
    uint32_t newest = 0;
    for (unsigned slot = 0; slot < LOG_CAPACITY; ++slot) {
        if (slot != 0 && slot % 64 == 0) {
            vTaskDelay(1);
        }
        log_record_t record;
        esp_err_t result = read_slot(slot, &record);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        if (result != ESP_OK) {
            nvs_close(storage);
            ESP_RETURN_ON_ERROR(result, TAG, "read log slot %u", slot);
        }
        if (record.number > newest) {
            newest = record.number;
        }
    }
    atomic_store(&latest_number, newest);
    boot_id = newest + 1;

    pending_records = xQueueCreate(QUEUE_LENGTH, sizeof(log_record_t));
    if (pending_records == NULL ||
        xTaskCreate(log_task, "twr_log", 4096, NULL, 4, NULL) != pdPASS) {
        if (pending_records != NULL) {
            vQueueDelete(pending_records);
            pending_records = NULL;
        }
        nvs_close(storage);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ranging_log_submit(const ranging_log_measurement_t *measurement)
{
    if (pending_records == NULL) {
        return;
    }

    const log_record_t record = {
        .uptime_ms = esp_timer_get_time() / 1000,
        .measurement = *measurement,
    };
    if (xQueueSend(pending_records, &record, 0) != pdPASS) {
        ESP_LOGW(TAG, "Log queue full, dropping measurement");
    }
}

esp_err_t ranging_log_download(httpd_req_t *request)
{
    if (pending_records == NULL) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "Local ranging log is unavailable.");
    }

    uint32_t last = atomic_load(&latest_number);
    uint32_t first = last > LOG_CAPACITY ? last - LOG_CAPACITY + 1 : 1;
    httpd_resp_set_type(request, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Disposition", "attachment; filename=\"twr-log.csv\"");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");

    char chunk[1024] = "record,boot_id,uptime_ms,role,tag_id,anchor_id,sequence,"
                       "round_a_ticks,reply_a_ticks,round_b_ticks,reply_b_ticks,distance_m,"
                       "poll_rx_timestamp_low32,response_tx_timestamp_low32,final_rx_timestamp_low32\r\n";
    size_t used = strlen(chunk);
    for (uint32_t number = first; number <= last; ++number) {
        log_record_t record;
        esp_err_t result = read_slot((number - 1) % LOG_CAPACITY, &record);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Could not read log record %" PRIu32 ": %s", number, esp_err_to_name(result));
            return result;
        }
        if (record.number != number) {
            continue; // The writer reclaimed this slot while the download was in progress.
        }

        const ranging_log_measurement_t *m = &record.measurement;
        char row[192];
        int length = snprintf(row, sizeof(row), "%" PRIu32 ",%" PRIu32 ",%" PRId64 ",%s,%u,%u,%u,",
                              number, record.boot_id, record.uptime_ms,
                              m->is_anchor ? "anchor" : "tag", m->tag_id, m->anchor_id, m->sequence);
        if (m->is_anchor) {
            length += snprintf(row + length, sizeof(row) - length,
                               ",,%" PRIu32 ",%" PRIu32 ",,%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\r\n",
                               m->round_b_ticks, m->reply_b_ticks, m->poll_rx_timestamp_low32,
                               m->response_tx_timestamp_low32, m->final_rx_timestamp_low32);
        } else {
            length += snprintf(row + length, sizeof(row) - length,
                               "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%.3f,,,\r\n",
                               m->round_a_ticks, m->reply_a_ticks, m->round_b_ticks, m->reply_b_ticks,
                               (double)m->distance_m);
        }
        if (used + length > sizeof(chunk)) {
            ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, chunk, used), TAG, "send log chunk");
            used = 0;
        }
        memcpy(chunk + used, row, length);
        used += length;
    }
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(request, chunk, used), TAG, "send log chunk");
    return httpd_resp_send_chunk(request, NULL, 0);
}
