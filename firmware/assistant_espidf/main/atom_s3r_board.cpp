#include "atom_s3r_board.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "atom_s3r";
constexpr int kTimeoutMs = 100;
i2c_master_bus_handle_t g_internal_bus;
i2c_master_dev_handle_t g_backlight;
SemaphoreHandle_t g_lock;
bool g_ready;
}

esp_err_t atom_s3r_backlight_init() {
    if (g_ready) return ESP_OK;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_internal_bus) {
        i2c_master_bus_config_t bus = {};
        bus.i2c_port = I2C_NUM_0;
        bus.sda_io_num = GPIO_NUM_45;
        bus.scl_io_num = GPIO_NUM_0;
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        bus.flags.enable_internal_pullup = true;
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_internal_bus), TAG, "internal I2C0");
    }
    if (!g_backlight) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x30;
        device.scl_speed_hz = 100000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_internal_bus, &device, &g_backlight), TAG, "LP5562 device");
    }
    // W PWM0e and W mapping70[7:6] belong to LCD backlight. Preserve RGB
    // mapping/current/engine state. Start dark before enabling direct W PWM.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_backlight, 0x0e, 0xff, 0, kTimeoutMs, TAG), TAG, "backlight off");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_backlight, 0x00, 0xc0, 0x40, kTimeoutMs, TAG), TAG, "LP5562 linear enable");
    vTaskDelay(pdMS_TO_TICKS(1));
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_backlight, 0x08, 0x41, 0x41, kTimeoutMs, TAG), TAG, "LP5562 internal clock/PWM frequency");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_backlight, 0x70, 0xc0, 0, kTimeoutMs, TAG), TAG, "LP5562 W direct mapping");
    g_ready = true;
    return ESP_OK;
}

esp_err_t atom_s3r_set_brightness(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const uint8_t pwm = percent * 255 / 100;
    const esp_err_t result = update_i2c_register_bits(g_backlight, 0x0e, 0xff, pwm, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}
