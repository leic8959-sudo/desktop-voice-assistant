#pragma once

#include "driver/i2c_master.h"
#include "esp_log.h"

inline esp_err_t update_i2c_register_bits(i2c_master_dev_handle_t device, uint8_t reg, uint8_t mask, uint8_t bits, int timeout_ms, const char *tag) {
    uint8_t actual = 0;
    esp_err_t result = i2c_master_transmit_receive(device, &reg, 1, &actual, 1, timeout_ms);
    const uint8_t value = (actual & ~mask) | (bits & mask);
    if (result == ESP_OK && value != actual) {
        const uint8_t write[] = {reg, value};
        result = i2c_master_transmit(device, write, sizeof(write), timeout_ms);
    }
    if (result == ESP_OK) result = i2c_master_transmit_receive(device, &reg, 1, &actual, 1, timeout_ms);
    if (result == ESP_OK && (actual & mask) != (value & mask)) result = ESP_ERR_INVALID_RESPONSE;
    if (result != ESP_OK) ESP_LOGE(tag, "register_update_failed reg=0x%02x mask=0x%02x error=%s", reg, mask, esp_err_to_name(result));
    return result;
}
