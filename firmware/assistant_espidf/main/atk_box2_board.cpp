#include "atk_box2_board.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "atk_box2";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready, g_wait_for_release = true;
esp_err_t g_last_key_error = ESP_OK;
}

esp_err_t atk_box2_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_device) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x20;
        device.scl_speed_hz = 100000;
        const esp_err_t result = i2c_master_bus_add_device(bus, &device, &g_device);
        if (result != ESP_OK) return result;
    }
    // Establish USB_SEL8/SYS_POW10/3V3A13 high, PA9/VBUS11 low before output
    // direction. Preserve4G12, SBU3/4 and every unowned output/config bit.
    esp_err_t result = update_i2c_register_bits(g_device, 0x03, 0x2f, 0x25, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x07, 0x2f, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x06, 0xe0, 0xe0, kTimeoutMs, TAG);
    // CHG_CTRL14 is normally high impedance; sampling/poweroff policies are
    // separate. Never turn it into a driven output during ordinary startup.
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x07, 0xc0, 0xc0, kTimeoutMs, TAG);
    if (result != ESP_OK) return result;
    gpio_config_t right_key = {};
    right_key.pin_bit_mask = 1ULL << GPIO_NUM_0;
    right_key.mode = GPIO_MODE_INPUT;
    right_key.pull_up_en = GPIO_PULLUP_ENABLE;
    result = gpio_config(&right_key);
    g_ready = result == ESP_OK;
    if (g_ready) ESP_LOGI(TAG, "rails_ready PA=off charge_control=input 4G=preserved");
    return result;
}

esp_err_t atk_box2_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_device, 0x03, 0x02, enabled ? 0x02 : 0, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}

ManualInputKey atk_box2_read_keys() {
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
    const bool middle = (input & 0x80) != 0;
    const bool left = (input & 0x20) == 0, right = !gpio_get_level(GPIO_NUM_0);
    const int count = middle + left + right;
    if (!count) { g_wait_for_release = false; return ManualInputKey::None; }
    if (g_wait_for_release || count > 1) return ManualInputKey::Invalid;
    return middle ? ManualInputKey::Listen : left ? ManualInputKey::VolumeDown : ManualInputKey::VolumeUp;
}

esp_err_t atk_box2_set_modem_enable(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    // Same device/lock as the PA path: do not create a second owner of bank1.
    // Establish the latch first, then output direction; all other bits retain
    // their current values, including system/analog rails and charge control.
    esp_err_t result = update_i2c_register_bits(g_device, 0x03, 0x10, enabled ? 0x10 : 0, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x05, 0x10, 0, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x07, 0x10, 0, kTimeoutMs, TAG);
    if (result == ESP_OK) {
        const uint8_t input_register = 0x01;
        uint8_t input = 0;
        result = i2c_master_transmit_receive(g_device, &input_register, 1, &input, 1, kTimeoutMs);
        if (result == ESP_OK && ((input & 0x10) != 0) != enabled) result = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(g_lock);
    if (result != ESP_OK) ESP_LOGE(TAG, "modem_enable_unconfirmed requested=%d error=%s", enabled, esp_err_to_name(result));
    else ESP_LOGI(TAG, "modem_enable_control=%d pin_readback=ok rail_reset_acceptance=not_run", enabled);
    return result;
}
