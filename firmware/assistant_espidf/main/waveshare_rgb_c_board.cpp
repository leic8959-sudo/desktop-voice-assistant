#include "waveshare_rgb_c_board.h"

#include "ch32_output.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "waveshare_rgb_c";
constexpr uint8_t kTouchReset = (1u << 1);
constexpr uint8_t kBacklightEnable = (1u << 2);
constexpr uint8_t kAmplifier = (1u << 3);
Ch32Output g_expander;
SemaphoreHandle_t g_lock;
bool g_ready, g_started;
}

esp_err_t waveshare_rgb_c_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return g_expander.failed() ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (!bus || g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    // Vendor2.0.0 initializes the entire port to directionFF/output00.
    // Its output/direction getters return a cache, not hardware readback.
    // Do not claim unrelated battery/power bits survive this constructor.
    ESP_RETURN_ON_ERROR(g_expander.Init(bus), TAG, "CH32 whole-port initialization");
    ESP_RETURN_ON_ERROR(g_expander.SetPwm(255), TAG, "backlight dark request");
    // Direction is alreadyFF; former set_dir was a cache no-op.
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kAmplifier, 0), TAG, "PA off request");
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kBacklightEnable, 1), TAG, "backlight enable request");
#if defined(ASSISTANT_BOARD_WS_RGB_7C)
    ESP_RETURN_ON_ERROR(g_expander.SetOutput((1u << 6), 1), TAG, "7C P6 request");
#endif
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kTouchReset, 0), TAG, "touch reset assert");
    vTaskDelay(pdMS_TO_TICKS(200));
    // GT911 latches0x5D when INT is low at reset release. The reset pin is
    // behind CH32, so perform address selection here before creating GT911.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_4, 0), TAG, "touch INT low latch");
    gpio_config_t interrupt = {};
    interrupt.pin_bit_mask = 1ULL << GPIO_NUM_4;
    interrupt.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "touch INT address select");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(g_expander.SetOutput(kTouchReset, 1), TAG, "touch reset release");
    vTaskDelay(pdMS_TO_TICKS(200));
    interrupt.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "release touch INT");
    g_ready = true;
    ESP_LOGI(TAG, "initialization requests accepted; PAoff/backlightdark/touch5D; CH32 whole-port reset and cached state");
    return ESP_OK;
}

esp_err_t waveshare_rgb_c_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = g_expander.SetOutput(kAmplifier, enabled ? 1 : 0);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t waveshare_rgb_c_set_brightness(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = g_expander.SetPwm(static_cast<uint8_t>((100 - percent) * 255 / 100));
    xSemaphoreGive(g_lock);
    return result;
}
