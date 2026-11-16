#pragma once
#include "esp_http_server.h"

bool headless_portal_authorized(httpd_req_t *request);
esp_err_t headless_portal_start(esp_err_t (*configure_handler)(httpd_req_t *));
