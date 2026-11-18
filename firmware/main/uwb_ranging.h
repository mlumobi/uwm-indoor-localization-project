#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t tag_id;
    uint16_t anchor_id;
    uint8_t sequence;
    uint32_t round_a_ticks;
    uint32_t reply_a_ticks;
    uint32_t round_b_ticks;
    uint32_t reply_b_ticks;
    int64_t completed_at_us;
} uwb_twr_sample_t;

enum { UWB_MAX_ANCHOR_TAGS = 32 };

typedef struct {
    uint16_t tag_id;
    uint16_t anchor_id;
    uint8_t sequence;
    int32_t distance_mm;
    uint32_t boot_id;
    uint32_t record_id;
    int64_t completed_at_us;
} uwb_anchor_sample_t;

esp_err_t uwb_ranging_start(void);
bool uwb_ranging_get_latest(uwb_twr_sample_t *sample);
size_t uwb_ranging_get_anchor_samples(uint16_t anchor_id, uwb_anchor_sample_t samples[UWB_MAX_ANCHOR_TAGS]);
