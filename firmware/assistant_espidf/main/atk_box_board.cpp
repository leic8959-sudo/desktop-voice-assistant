#include "atk_box_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "atk_box";
constexpr int kTimeoutMs = 100;
AtkBoxAudio g_audio = AtkBoxAudio::Uninitialized;
}

AtkBoxAudio atk_box_audio() { return g_audio; }

esp_err_t atk_box_board_init(i2c_master_bus_handle_t bus) {
    if (g_audio != AtkBoxAudio::Uninitialized) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x20;
    config.scl_speed_hz = 100000;
    i2c_master_dev_handle_t device = nullptr;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &device), TAG, "XL9555 device");
    // The owning source first configures0x3B/0xFE, samples port0 bit5, then
    // configures0x1B/0xFE. Preserve every output latch and unrelated direction.
    esp_err_t result = update_i2c_register_bits(device, 0x06, 0xe4, 0x20, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(device, 0x07, 0x01, 0x00, kTimeoutMs, TAG);
    const uint8_t input_register = 0x00;
    uint8_t selected = 0;
    for (int sample = 0; result == ESP_OK && sample < 3; ++sample) {
        uint8_t input = 0;
        result = i2c_master_transmit_receive(device, &input_register, 1, &input, 1, kTimeoutMs);
        if (result == ESP_OK && sample && (input & 0x20) != selected) result = ESP_ERR_INVALID_RESPONSE;
        if (result == ESP_OK) selected = input & 0x20;
        if (result == ESP_OK && sample < 2) vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (result == ESP_OK) result = update_i2c_register_bits(device, 0x06, 0xe4, 0x00, kTimeoutMs, TAG);
    const esp_err_t cleanup = i2c_master_bus_rm_device(device);
    if (result == ESP_OK) result = cleanup;
    ESP_RETURN_ON_ERROR(result, TAG, "XL9555 checked codec selection");
    g_audio = selected ? AtkBoxAudio::Es8311 : AtkBoxAudio::Digital;
    ESP_LOGI(TAG, "audio_selected=%s input_bit5=%u", selected ? "ES8311" : "digital", selected ? 1 : 0);
    return ESP_OK;
}
