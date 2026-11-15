#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t node_dashboard_start(void);
void node_dashboard_set_radio_id(uint32_t id);
void node_dashboard_get_ranging_config(uint16_t *id, bool *anchor);
