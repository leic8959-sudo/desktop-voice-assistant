#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

struct DeviceConfig {
    char wifi_ssid[64];
    char wifi_password[64];
    char ws_url[192];
    char device_token[192];
};

esp_err_t device_config_load(DeviceConfig *config);
esp_err_t device_config_save(const DeviceConfig *config);
bool device_config_ready(const DeviceConfig *config);
bool device_config_network_ready(const DeviceConfig *config);
