#include "atk_xl9555.h"

#include "i2c_register_bits.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *TAG = "atk_xl9555";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t update_bits(uint8_t reg, uint8_t mask, uint8_t bits) {
    return update_i2c_register_bits(g_device, reg, mask, bits, kTimeoutMs, TAG);
}

esp_err_t set_output(uint8_t reg, uint8_t mask, bool high) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_bits(reg, mask, high ? mask : 0);
    xSemaphoreGive(g_lock);
    return result;
}
}

esp_err_t atk_xl9555_init(i2c_master_bus_handle_t bus) {
    // Own SPK_EN(bit2), LCD_BL(bit8), SLCD_RST(bit10) and SLCD_PWR(bit11).
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
    // XL9555 has distinct output/config banks. Never copy PCA9557 addresses or
    // overwrite camera, touch reset, buzzer and key bits with whole-port values.
    esp_err_t result = update_bits(0x02, 0x04, 0x04);
    if (result == ESP_OK) result = update_bits(0x03, 0x0d, 0x0c);
    if (result == ESP_OK) result = update_bits(0x06, 0x04, 0x00);
    if (result == ESP_OK) result = update_bits(0x07, 0x0d, 0x00);
    g_ready = result == ESP_OK;
    if (g_ready) ESP_LOGI(TAG, "initialized LCD_power=on reset=released BL=off SPK_EN=high_disabled");
    return result;
}

esp_err_t atk_xl9555_set_backlight(bool enabled) { return set_output(0x03, 0x01, enabled); }
esp_err_t atk_xl9555_set_amplifier(bool enabled) { return set_output(0x02, 0x04, !enabled); }
