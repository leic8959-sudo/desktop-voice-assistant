#pragma once
#include "device_config.h"
#include "esp_err.h"
esp_err_t wifi_prepare();

esp_err_t wifi_start(const DeviceConfig *config);
esp_err_t provisioning_start(DeviceConfig *config);
