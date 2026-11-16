#include "waveshare_audio_board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "waveshare_audio";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready;
}

esp_err_t waveshare_audio_board_init(i2c_master_bus_handle_t bus) {
    // Single-threaded startup; only reset0/1 and PA8 are owned here.
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
    esp_err_t result = update_i2c_register_bits(g_device, 0x03, 0x01, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x02, 0x03, 0x03, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x07, 0x01, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x06, 0x03, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x02, 0x03, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    if (result == ESP_OK) result = update_i2c_register_bits(g_device, 0x02, 0x03, 0x03, kTimeoutMs, TAG);
    g_ready = result == ESP_OK;
    if (g_ready) ESP_LOGI(TAG, "reset0-1=released PA8=off camera5-6=preserved");
    return result;
}

esp_err_t waveshare_audio_board_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_device, 0x03, 0x01, enabled ? 0x01 : 0, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}
