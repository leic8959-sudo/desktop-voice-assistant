#include "pca9557_box.h"
#include "i2c_register_bits.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *TAG = "pca9557_box";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t update_bits(uint8_t reg, uint8_t mask, uint8_t bits) {
    return update_i2c_register_bits(g_device, reg, mask, bits, kTimeoutMs, TAG);
}

esp_err_t set_output(uint8_t mask, bool high) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_bits(0x01, mask, high ? mask : 0);
    xSemaphoreGive(g_lock);
    return result;
}
}

esp_err_t pca9557_box_init(i2c_master_bus_handle_t bus) {
    // Called during single-threaded board startup, before display/audio tasks.
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_device) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x19;
        device.scl_speed_hz = 400000;
        const esp_err_t result = i2c_master_bus_add_device(bus, &device, &g_device);
        if (result != ESP_OK) return result;
    }
    // Establish LCD control high/PA low before changing direction. Preserve
    // camera bit2 and every unrelated output/config bit from the actual device.
    esp_err_t result = update_bits(0x01, 0x03, 0x01);
    if (result == ESP_OK) result = update_bits(0x03, 0x03, 0x00);
    g_ready = result == ESP_OK;
    if (g_ready) ESP_LOGI(TAG, "initialized LCD_control=high amplifier=off");
    return result;
}

esp_err_t pca9557_box_set_panel_control(bool high) { return set_output(0x01, high); }
esp_err_t pca9557_box_set_amplifier(bool enabled) { return set_output(0x02, enabled); }
