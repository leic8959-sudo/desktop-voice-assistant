// SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Polling protocol adapted from esp-iot-solution@88152bff24d5fbc05ff2ffed3d5208c247711ded.
// License: third_party/spd2010/LICENSE. Only board-owned polling is provided.
#include "spd2010_touch.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

namespace { constexpr const char *TAG = "spd2010_touch"; }

esp_err_t Spd2010Touch::ReadRegister(uint8_t low, uint8_t high, uint8_t *data, size_t size) {
    const uint8_t address[] = {low, high};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io_, -1, address, sizeof(address)), TAG, "register select");
    esp_rom_delay_us(200);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(io_, -1, data, size), TAG, "register read");
    esp_rom_delay_us(200);
    return ESP_OK;
}

esp_err_t Spd2010Touch::Command(uint8_t command, uint8_t value) {
    const uint8_t data[] = {command, 0x00, value, 0x00};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io_, -1, data, sizeof(data)), TAG, "command0x%02x", command);
    esp_rom_delay_us(200);
    return ESP_OK;
}

esp_err_t Spd2010Touch::Init(esp_lcd_panel_io_handle_t io) {
    if (!io || io_) return ESP_ERR_INVALID_ARG;
    io_ = io;
    uint8_t version[18]{};
    const esp_err_t result = ReadRegister(0x26, 0x00, version, sizeof(version));
    if (result != ESP_OK) { io_ = nullptr; return result; }
    ESP_LOGI(TAG, "firmware=%02x%02x product=%02x%02x%02x%02x;address53/reset=board-owned/physical identity unconfirmed",
        version[5], version[4], version[9], version[8], version[7], version[6]);
    return ESP_OK;
}

esp_err_t Spd2010Touch::Poll(Spd2010Frame *frame) {
    const int64_t deadline = esp_timer_get_time() + 1000000;
    uint8_t status[4]{};
    ESP_RETURN_ON_ERROR(ReadRegister(0x20, 0x00, status, sizeof(status)), TAG, "status");
    const uint8_t low = status[0], high = status[1];
    const uint16_t length = status[2] | (static_cast<uint16_t>(status[3]) << 8);
    if (high & 0x40) {
        // BIOS and CPU-mode handshakes are not release reports.
        ESP_RETURN_ON_ERROR(Command(0x02, 0x01), TAG, "BIOS clear");
        ESP_RETURN_ON_ERROR(Command(0x04, 0x01), TAG, "CPU start");
        return ESP_ERR_NOT_FINISHED;
    }
    if (high & 0x20) {
        ESP_RETURN_ON_ERROR(Command(0x50, 0x00), TAG, "point mode");
        ESP_RETURN_ON_ERROR(Command(0x46, 0x00), TAG, "touch start");
        ESP_RETURN_ON_ERROR(Command(0x02, 0x01), TAG, "CPU clear");
        return ESP_ERR_NOT_FINISHED;
    }
    if ((high & 0x08) && length == 0) {
        ESP_RETURN_ON_ERROR(Command(0x02, 0x01), TAG, "zero-length clear");
        *frame = {};
        return ESP_OK;
    }
    if (!(low & 0x03)) {
        if ((high & 0x08) && (low & 0x08)) {
            ESP_RETURN_ON_ERROR(Command(0x02, 0x01), TAG, "aux clear");
        }
        // Unrelated/idle status does not manufacture a release. A snapshot
        // may still become stale if the controller never reports new data.
        if (!valid_) return ESP_ERR_NOT_FINISHED;
        *frame = last_;
        return ESP_OK;
    }
    if (length < 5 || length > 64) return ESP_ERR_INVALID_RESPONSE;
    uint8_t packet[64]{};
    ESP_RETURN_ON_ERROR(ReadRegister(0x00, 0x03, packet, length), TAG, "HDP");
    Spd2010Frame next{};
    bool unsupported = true;
    if ((low & 0x01) && packet[4] <= 0x0a) {
        if ((length - 4) % 6) return ESP_ERR_INVALID_RESPONSE;
        next.count = (length - 4) / 6;
        if (!next.count || next.count > 10) return ESP_ERR_INVALID_RESPONSE;
        unsupported = false;
        for (uint8_t index = 0; index < next.count; ++index) {
            const uint8_t *point = packet + 4 + index * 6;
            if (point[0] > 0x0a) unsupported = true;
            next.points[index] = {point[0], static_cast<uint16_t>(((point[3] & 0xf0) << 4) | point[1]),
                static_cast<uint16_t>(((point[3] & 0x0f) << 8) | point[2]), point[4]};
            for (uint8_t previous = 0; previous < index; ++previous) {
                if (next.points[previous].id == point[0]) unsupported = true;
            }
        }
    }
    // Gestures and unknown report types are acknowledged but never accepted
    // as a fresh press or release. No gesture-driven product action is added.
    size_t drained = 0;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        uint8_t complete[8]{};
        ESP_RETURN_ON_ERROR(ReadRegister(0xfc, 0x02, complete, sizeof(complete)), TAG, "HDP status");
        if (complete[5] == 0x82) {
            ESP_RETURN_ON_ERROR(Command(0x02, 0x01), TAG, "HDP clear");
            if (unsupported) return ESP_ERR_INVALID_RESPONSE;
            *frame = next;
            return ESP_OK;
        }
        if (complete[5] != 0) return ESP_ERR_INVALID_RESPONSE;
        const uint16_t bytes = complete[2] | (static_cast<uint16_t>(complete[3]) << 8);
        if (!bytes || bytes > sizeof(packet) || drained + bytes > 512) return ESP_ERR_INVALID_RESPONSE;
        ESP_RETURN_ON_ERROR(ReadRegister(0x00, 0x03, packet, bytes), TAG, "remaining HDP");
        drained += bytes;
        // The fixed Waveshare Arduino/IDF examples and Espressif2.1.0 parse
        // coordinates from the first HDP read only; remaining data is drained
        // separately. Keep that report until status82 and clear both succeed.
        // Do not append unknown trailing bytes to it or reject a validated
        // report solely because the controller had remaining data to drain.
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t Spd2010Touch::Read(Spd2010Frame *frame) {
    if (!frame) return ESP_ERR_INVALID_ARG;
    *frame = {};
    if (!io_) return ESP_ERR_INVALID_STATE;
    Spd2010Frame next{};
    const esp_err_t result = Poll(&next);
    if (result != ESP_OK) { valid_ = false; return result; }
    last_ = next;
    valid_ = true;
    *frame = next;
    return ESP_OK;
}
