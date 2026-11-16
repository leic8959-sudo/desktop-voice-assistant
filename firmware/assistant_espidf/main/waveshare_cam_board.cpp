// SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// CH32 protocol adapted from Waveshare-ESP32-components@9daffb7168b3c093a330da700988a22092294eef.
// License: third_party/ch32v003/LICENSE. Only this board's reset/PA/PWM path is provided.
#include "waveshare_cam_board.h"
#include "ch32_output.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "waveshare_cam";
constexpr uint8_t kResets = (1u << 0) | (1u << 1);
constexpr uint8_t kAmplifier = 1u << 4, kPowerLed = 1u << 6;
Ch32Output g_expander;
SemaphoreHandle_t g_lock;
bool g_ready, g_started;

esp_err_t initialize_board(i2c_master_bus_handle_t bus) {
    ESP_RETURN_ON_ERROR(g_expander.Init(bus), TAG, "CH32 whole-port initialization");
    ESP_RETURN_ON_ERROR(g_expander.SetPwm(255), TAG, "backlight initially dark");
    // All direction bits are already1. The former set_dir is a cached no-op.
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kAmplifier, false), TAG, "PA off request");
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kResets, true), TAG, "reset initial high");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kResets, false), TAG, "reset low");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kResets, true), TAG, "reset release");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kPowerLed, true), TAG, "power LED request");
    ESP_LOGI(TAG, "CH32 initialization requests accepted; whole-port reset, cached state, no physical readback");
    return ESP_OK;
}
}

esp_err_t waveshare_cam_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return g_expander.failed() ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (!bus || g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    const esp_err_t result = initialize_board(bus);
    g_ready = result == ESP_OK;
    return result;
}

esp_err_t waveshare_cam_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = g_expander.SetOutput(kAmplifier, enabled);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t waveshare_cam_set_brightness(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const uint8_t duty = static_cast<uint8_t>((100 - percent) * 255 / 100);
    const esp_err_t result = g_expander.SetPwm(duty);
    xSemaphoreGive(g_lock);
    return result;
}
