// SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Output protocol adapted from Waveshare-ESP32-components@9daffb7168b3c093a330da700988a22092294eef.
// License: third_party/ch32v003/LICENSE.
#include "ch32_output.h"

#include "esp_check.h"
#include "esp_log.h"

namespace { constexpr const char *TAG = "ch32_output"; }

esp_err_t Ch32Output::Write(uint8_t address, uint8_t value) {
    if (failed_ || !device_) return ESP_ERR_INVALID_STATE;
    const uint8_t data[] = {address, value};
    const esp_err_t result = i2c_master_transmit(device_, data, sizeof(data), 1000);
    if (result != ESP_OK) {
        // A timed-out write may have reached the module. No further write
        // uses this uncertain cache; the application must restart to retry.
        failed_ = true;
        ESP_LOGE(TAG, "write failed reg=0x%02x value=0x%02x error=%s;restart required", address, value, esp_err_to_name(result));
    }
    return result;
}

esp_err_t Ch32Output::Init(i2c_master_bus_handle_t bus) {
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (started_) return ESP_ERR_INVALID_STATE;
    started_ = true;
    i2c_device_config_t device = {};
    device.device_address = 0x24;
    device.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &device_), TAG, "I2C device");
    // Match the reviewed constructor:1meansoutput, all direction/output bits
    // are reset. This does not preserve unowned camera/battery/SD/power bits.
    ESP_RETURN_ON_ERROR(Write(0x02, 0xff), TAG, "whole-port direction");
    ESP_RETURN_ON_ERROR(Write(0x03, 0x00), TAG, "whole-port output reset");
    output_ = 0;
    initialized_ = true;
    return ESP_OK;
}

esp_err_t Ch32Output::SetOutput(uint8_t mask, bool enabled) {
    if (!initialized_ || failed_) return ESP_ERR_INVALID_STATE;
    const uint8_t output = enabled ? output_ | mask : output_ & ~mask;
    // Preserve cached/no-change behavior. An independent CH32 reset is not
    // detected, and matching cache values do not prove physical pad state.
    if (output == output_) return ESP_OK;
    ESP_RETURN_ON_ERROR(Write(0x03, output), TAG, "output update");
    output_ = output;
    return ESP_OK;
}

esp_err_t Ch32Output::SetPwm(uint8_t value) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    return Write(0x05, value);
}
