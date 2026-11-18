#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef struct {
    uint32_t round_a_ticks;
    uint32_t reply_a_ticks;
    uint32_t round_b_ticks;
    uint32_t reply_b_ticks;
    uint32_t poll_rx_timestamp_low32;
    uint32_t response_tx_timestamp_low32;
    uint32_t final_rx_timestamp_low32;
    float distance_m;
    uint16_t tag_id;
    uint16_t anchor_id;
    uint8_t sequence;
    bool is_anchor;
} ranging_log_measurement_t;

esp_err_t ranging_log_start(void);
void ranging_log_submit(const ranging_log_measurement_t *measurement);
esp_err_t ranging_log_download(httpd_req_t *request);
