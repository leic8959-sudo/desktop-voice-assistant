#include "moji2_c5_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *kTag = "moji2_c5";
constexpr gpio_num_t kAmplifier = GPIO_NUM_5, kBacklight = GPIO_NUM_2;
SemaphoreHandle_t g_lock;
bool g_ready;
}

esp_err_t moji2_c5_board_init() {
    if (g_ready) return ESP_OK;
    // Keep PA off and the display dark through codec/model initialization.
    // GPIO10 LED and GPIO4 battery ADC are outside this implementation.
    ESP_RETURN_ON_ERROR(gpio_set_level(kAmplifier, 0), kTag, "PA off latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(kBacklight, 0), kTag, "backlight dark latch");
    gpio_config_t output = {};
    output.pin_bit_mask = 1ULL << kAmplifier; output.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "PA pad with readback");
    if (gpio_get_level(kAmplifier)) return ESP_ERR_INVALID_RESPONSE;
    output.pin_bit_mask = 1ULL << kBacklight; output.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "backlight output");
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), kTag, "codec I2C0");
    g_ready = true;
    return ESP_OK;
}

esp_err_t moji2_c5_set_amplifier(bool enabled) {
    if (!g_ready || !g_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = gpio_set_level(kAmplifier, enabled ? 1 : 0);
    if (result == ESP_OK && gpio_get_level(kAmplifier) != (enabled ? 1 : 0)) result = ESP_ERR_INVALID_RESPONSE;
    xSemaphoreGive(g_lock);
    if (result != ESP_OK) ESP_LOGE(kTag, "PA update failed enabled=%d error=%s", enabled, esp_err_to_name(result));
    return result;
}
