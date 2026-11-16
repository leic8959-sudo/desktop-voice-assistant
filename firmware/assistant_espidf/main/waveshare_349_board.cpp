#include "waveshare_349_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "waveshare_349";
constexpr uint8_t kAmplifier = 0x80, kSystemHold = 0x40;
#if defined(ASSISTANT_BOARD_WS349_V2)
constexpr gpio_num_t kBacklight = GPIO_NUM_42;
constexpr uint8_t kLcdReset = 0x20, kBacklightEnable = 0x02;
#else
constexpr gpio_num_t kBacklight = GPIO_NUM_8;
#endif
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready, g_poweroff_requested;

esp_err_t check_amplifier(bool enabled) {
    const uint8_t input_register = 0;
    uint8_t input = 0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(g_device, &input_register, 1, &input, 1, 100), TAG, "PA input readback");
    if (((input & kAmplifier) != 0) != enabled) {
        ESP_LOGE(TAG, "PA level mismatch expected=%d input=0x%02x", enabled, input);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

void power_button_task(void *) {
    bool released = false;
    int64_t pressed_at = 0;
    while (true) {
        if (gpio_get_level(GPIO_NUM_16)) { released = true; pressed_at = 0; }
        else if (released) {
            const int64_t now = esp_timer_get_time();
            if (!pressed_at) pressed_at = now;
            // Same two-second manual hold as existing local power buttons.
            // Never release the supply in response to the initial boot hold.
            if (now - pressed_at >= 2000000) {
                if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_poweroff_requested = true;
                    const esp_err_t result = update_i2c_register_bits(g_device, 0x01,
                        kAmplifier | kSystemHold, 0, 100, TAG);
                    xSemaphoreGive(g_lock);
                    // Power loss can prevent the register readback. On USB
                    // power may remain; block PA re-enable until restart.
                    ESP_LOGI(TAG, "manual_poweroff_request result=%s; physical power state unconfirmed", esp_err_to_name(result));
                    released = false;
                    pressed_at = 0;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}

esp_err_t waveshare_349_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(gpio_set_level(kBacklight, 1), TAG, "active-low backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << kBacklight;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight dark output");
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_device) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x20;
        device.scl_speed_hz = 400000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &g_device), TAG, "TCA9554 device");
    }
    uint8_t outputs = kAmplifier | kSystemHold;
#if defined(ASSISTANT_BOARD_WS349_V2)
    outputs |= kLcdReset | kBacklightEnable;
#endif
    // Preserve every unowned TCA bit; no whole-port constructor reset.
    // P6 holds system power; P7 remains low until actual DAC unmute succeeds.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01, outputs, kSystemHold, 100, TAG), TAG, "power hold/PA off/reset latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x03, outputs, 0, 100, TAG), TAG, "owned output directions");
    ESP_RETURN_ON_ERROR(check_amplifier(false), TAG, "PA off confirmation");
#if defined(ASSISTANT_BOARD_WS349_V2)
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01, kLcdReset, kLcdReset, 100, TAG), TAG, "LCD reset high");
#else
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_21, 1), TAG, "LCD reset high latch");
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << GPIO_NUM_21;
    reset.mode = GPIO_MODE_OUTPUT;
    reset.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "LCD reset21 output");
#endif
    vTaskDelay(pdMS_TO_TICKS(30));
#if defined(ASSISTANT_BOARD_WS349_V2)
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01, kLcdReset, 0, 100, TAG), TAG, "LCD reset assert");
#else
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_21, 0), TAG, "LCD reset assert");
#endif
    vTaskDelay(pdMS_TO_TICKS(250));
#if defined(ASSISTANT_BOARD_WS349_V2)
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01, kLcdReset, kLcdReset, 100, TAG), TAG, "LCD reset release");
#else
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_21, 1), TAG, "LCD reset release");
#endif
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_config_t key = {};
    key.pin_bit_mask = 1ULL << GPIO_NUM_16;
    key.mode = GPIO_MODE_INPUT;
    key.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&key), TAG, "power key input");
    g_ready = true;
    if (xTaskCreate(power_button_task, "power_button", 2048, nullptr, 3, nullptr) != pdPASS) {
        g_ready = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "TCA9554 power hold high/PA off/readback; LCD reset30-250-30ms; BLdark");
    return ESP_OK;
}

esp_err_t waveshare_349_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = enabled && g_poweroff_requested ? ESP_ERR_INVALID_STATE :
        update_i2c_register_bits(g_device, 0x01, kAmplifier, enabled ? kAmplifier : 0, 100, TAG);
    if (result == ESP_OK) result = check_amplifier(enabled);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t waveshare_349_set_backlight_enable(bool enabled) {
#if defined(ASSISTANT_BOARD_WS349_V2)
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = enabled && g_poweroff_requested ? ESP_ERR_INVALID_STATE :
        update_i2c_register_bits(g_device, 0x01, kBacklightEnable, enabled ? kBacklightEnable : 0, 100, TAG);
    xSemaphoreGive(g_lock);
    return result;
#else
    (void)enabled;
    return ESP_OK;
#endif
}
