#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint16_t id;
    uint16_t target_anchor_id; // Zero selects broadcast ranging.
    bool anchor;
} node_ranging_config_t;

typedef enum { TDOA_OFF, TDOA_TAG, TDOA_SECONDARY, TDOA_MAIN } node_tdoa_role_t;

typedef struct {
    node_tdoa_role_t role;
    uint16_t id;
    uint16_t main_id;
} node_tdoa_config_t;

esp_err_t node_dashboard_start(void);
void node_dashboard_set_radio_id(uint32_t id);
node_ranging_config_t node_dashboard_get_ranging_config(void);
node_tdoa_config_t node_dashboard_get_tdoa_config(void);
