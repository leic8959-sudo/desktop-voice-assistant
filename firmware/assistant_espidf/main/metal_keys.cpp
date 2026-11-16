#include "metal_keys.h"

#include "tft_board.h"
#include "esp_check.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "metal_keys";
i2c_master_dev_handle_t g_keys;
bool g_wait_for_release = true;
esp_err_t g_last_read_error = ESP_OK;
bool g_bad_code;
}

esp_err_t metal_keys_init() {
    if (g_keys) return ESP_OK;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "key I2C");
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x15;
    config.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bsp_i2c_get_handle(), &config, &g_keys), TAG, "CST816 key device");
    uint8_t identifier = 0;
    const uint8_t id_register = 0xA7;
    const esp_err_t result = i2c_master_transmit_receive(g_keys, &id_register, 1, &identifier, 1, 20);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "key_chip_read failed: %s", esp_err_to_name(result));
        const esp_err_t cleanup = i2c_master_bus_rm_device(g_keys);
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "key_device_cleanup failed: %s", esp_err_to_name(cleanup));
        g_keys = nullptr;
        return result;
    }
    // The source reads this identifier for diagnostics, without claiming all
    // CST816 revisions return the same chip ID or ordinary screen coordinates.
    ESP_LOGI(TAG, "key_chip_id=0x%02x address=0x15 format=discrete_keys", identifier);
    return ESP_OK;
}

ManualInputKey metal_keys_read() {
    if (!g_keys) return ManualInputKey::Invalid;
    const uint8_t first_register = 0x02;
    uint8_t data[6]{};
    const esp_err_t result = i2c_master_transmit_receive(g_keys, &first_register, 1, data, sizeof(data), 20);
    if (result != ESP_OK) {
        if (result != g_last_read_error) ESP_LOGE(TAG, "key_read failed: %s", esp_err_to_name(result));
        g_last_read_error = result;
        g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    if (g_last_read_error != ESP_OK) ESP_LOGI(TAG, "key_read recovered; waiting for full release");
    g_last_read_error = ESP_OK;
    const uint8_t count = data[0] & 0x0F;
    if (!count) {
        g_wait_for_release = false;
        g_bad_code = false;
        return ManualInputKey::None;
    }
    const int x = ((data[1] & 0x0F) << 8) | data[2];
    const int y = ((data[3] & 0x0F) << 8) | data[4];
    if (count != 1 || y != 600 || (x != 20 && x != 40 && x != 60)) {
        if (!g_bad_code) ESP_LOGE(TAG, "unsupported_key_code count=%u x=%d y=%d", count, x, y);
        g_bad_code = true;
        g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    if (g_wait_for_release) return ManualInputKey::Invalid;
    return x == 40 ? ManualInputKey::Listen : x == 20 ? ManualInputKey::VolumeUp : ManualInputKey::VolumeDown;
}
