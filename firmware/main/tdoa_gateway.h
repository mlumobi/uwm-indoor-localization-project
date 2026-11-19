#pragma once

#include "esp_http_server.h"
#include "node_dashboard.h"

void tdoa_run(node_tdoa_config_t config);
esp_err_t tdoa_serve_events(httpd_req_t *request);
