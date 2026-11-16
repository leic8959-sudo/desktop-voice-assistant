#include "stick_s3_power.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "stick_s3_power";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_pm1;
SemaphoreHandle_t g_lock;
bool g_ready;
}

esp_err_t stick_s3_power_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_pm1) {
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = 0x6e;
        config.scl_speed_hz = 100000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &config, &g_pm1), TAG, "PM1 device");
    }
    // The official driver wakes PM1 with a read, then retries after800ms.
    // Use bounded reads; a successful first read needs no forced wake cycle.
    const uint8_t identity_register = 0;
    uint8_t identity[4] = {};
    esp_err_t result = i2c_master_transmit_receive(g_pm1, &identity_register, 1, identity, sizeof(identity), kTimeoutMs);
    if (result != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(800));
        result = i2c_master_transmit_receive(g_pm1, &identity_register, 1, identity, sizeof(identity), kTimeoutMs);
    }
    ESP_RETURN_ON_ERROR(result, TAG, "PM1 identity read");
    // The pinned manufacturer library logs these bytes without specifying a
    // fixed ID/model. Report them rather than inventing a hardware revision.
    ESP_LOGI(TAG, "PM1 id=%02x model=%02x hw=%02x sw=%02x", identity[0], identity[1], identity[2], identity[3]);
    // PM1@1.0.7: OUT11,MODE10,DRIVE13,GPIO_FUNC16. Only G2/G3 are
    // owned here. PA G3 latch low before selecting GPIO/output, then power G2.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_pm1, 0x11, 0x08, 0, kTimeoutMs, TAG), TAG, "PA off latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_pm1, 0x16, 0xf0, 0, kTimeoutMs, TAG), TAG, "G2/G3 GPIO function");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_pm1, 0x13, 0x0c, 0, kTimeoutMs, TAG), TAG, "G2/G3 push pull");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_pm1, 0x10, 0x0c, 0x0c, kTimeoutMs, TAG), TAG, "G2/G3 output");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_pm1, 0x11, 0x0c, 0x04, kTimeoutMs, TAG), TAG, "LCD/audio power on PA off");
    vTaskDelay(pdMS_TO_TICKS(100));
    // GPIO12 is a real user key; keep it input until its product action is
    // implemented. GPIO11 is configured by the existing talk/card task.
    gpio_config_t user_key = {};
    user_key.pin_bit_mask = 1ULL << GPIO_NUM_12;
    user_key.mode = GPIO_MODE_INPUT;
    user_key.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&user_key), TAG, "user key input");
    g_ready = true;
    return ESP_OK;
}

esp_err_t stick_s3_set_amplifier(bool enabled) {
    if (!g_lock || !g_pm1 || (enabled && !g_ready)) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_pm1, 0x11, 0x08, enabled ? 0x08 : 0, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}
