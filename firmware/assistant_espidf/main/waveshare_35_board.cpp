#include "waveshare_35_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "waveshare_35";
constexpr uint8_t kAmplifier = 0x80, kLcdReset = 0x02;
i2c_master_dev_handle_t g_device;
SemaphoreHandle_t g_lock;
bool g_ready;

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
}

esp_err_t waveshare_35_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_6, 0), TAG, "backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_6;
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
    // Manufacturer schematics: P0 camera PWDN, P1 LCD_RST, P2 TP_INT,
    // P3 SD_CS, P4 RTC_INT, P5 AXP_IRQ, P6 SYS_OUT, P7 PA_CTRL.
    // Preserve every unowned bit. Do not invoke the vendor constructor's
    // whole-port FF/FF reset or drive camera PWDN as part of voice startup.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01,
        kAmplifier | kLcdReset, 0, 100, TAG), TAG, "PA off/LCD reset low latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x03,
        kAmplifier | kLcdReset, 0, 100, TAG), TAG, "PA/LCD output directions");
    ESP_RETURN_ON_ERROR(check_amplifier(false), TAG, "PA off confirmation");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01,
        kLcdReset, kLcdReset, 100, TAG), TAG, "LCD reset release");
    vTaskDelay(pdMS_TO_TICKS(100));
    g_ready = true;
    ESP_LOGI(TAG, "TCA9554 LCD reset completed; PA off/readback; unowned bits preserved; BLdark");
    return ESP_OK;
}

esp_err_t waveshare_35_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = update_i2c_register_bits(g_device, 0x01,
        kAmplifier, enabled ? kAmplifier : 0, 100, TAG);
    if (result == ESP_OK) result = check_amplifier(enabled);
    xSemaphoreGive(g_lock);
    return result;
}
