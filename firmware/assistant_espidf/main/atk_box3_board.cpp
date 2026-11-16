#include "atk_box3_board.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "atk_box3";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready, g_wait_for_release = true;
esp_err_t g_last_key_error = ESP_OK;
}

esp_err_t atk_box3_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_device) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x59;
        device.scl_speed_hz = 100000;
        const esp_err_t result = i2c_master_bus_add_device(bus, &device, &g_device);
        if (result != ESP_OK) return result;
    }
    // Verify AW9523B before touching rails. TCA9555 direction registers0x06/07
    // are interrupt masks here; GPIO directions are0x04/05 instead.
    const uint8_t id_register = 0x10;
    uint8_t identifier = 0;
    esp_err_t result = i2c_master_transmit_receive(g_device, &id_register, 1, &identifier, 1, kTimeoutMs);
    if (result == ESP_OK && identifier != 0x23) result = ESP_ERR_INVALID_RESPONSE;
    if (result != ESP_OK) { ESP_LOGE(TAG, "AW9523B_ID_failed value=0x%02x error=%s", identifier, esp_err_to_name(result)); return result; }
    // Set PA low and binary backlight high(off), rails11/12/13 high before
    // GPIO mode/direction. Preserve camera14/reset15, LEDs9/10 and EXT GPIOs.
    result = update_i2c_register_bits(g_device, 0x04, 0x0f, 0x0f, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x02, 0x20, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x03, 0x39, 0x39, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x12, 0x2f, 0x2f, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x13, 0x39, 0x39, kTimeoutMs, TAG);
    // P0 push-pull is a port-wide setting required by the owning board docs.
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x11, 0x10, 0x10, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x04, 0x20, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x05, 0x39, 0x00, kTimeoutMs, TAG);
    if (result != ESP_OK) return result;
    gpio_config_t right_key = {};
    right_key.pin_bit_mask = 1ULL << GPIO_NUM_0;
    right_key.mode = GPIO_MODE_INPUT;
    right_key.pull_up_en = GPIO_PULLUP_ENABLE;
    result = gpio_config(&right_key);
    g_ready = result == ESP_OK;
    if (g_ready) ESP_LOGI(TAG, "rails_ready AW9523B_ID=23 PA=off BL=off rails11-13=high charge_control=input");
    return result;
}

esp_err_t atk_box3_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_device, 0x02, 0x20, enabled ? 0x20 : 0, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t atk_box3_set_backlight(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_device, 0x03, 0x01, enabled ? 0 : 0x01, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}

ManualInputKey atk_box3_read_keys() {
    if (!g_ready) return ManualInputKey::Invalid;
    esp_err_t result = ESP_ERR_TIMEOUT;
    uint8_t input = 0;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        const uint8_t reg = 0x00;
        result = i2c_master_transmit_receive(g_device, &reg, 1, &input, 1, 20);
        xSemaphoreGive(g_lock);
    }
    if (result != ESP_OK) {
        if (result != g_last_key_error) ESP_LOGE(TAG, "key_read_failed error=%s", esp_err_to_name(result));
        g_last_key_error = result;
        g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    if (g_last_key_error != ESP_OK) ESP_LOGI(TAG, "key_read_recovered wait_for_release");
    g_last_key_error = ESP_OK;
    const bool middle = (input & 0x02) != 0;
    const bool left = (input & 0x01) == 0, right = !gpio_get_level(GPIO_NUM_0);
    const int count = middle + left + right;
    if (!count) { g_wait_for_release = false; return ManualInputKey::None; }
    if (g_wait_for_release || count > 1) return ManualInputKey::Invalid;
    return middle ? ManualInputKey::Listen : left ? ManualInputKey::VolumeDown : ManualInputKey::VolumeUp;
}
