#include "freenove_keys.h"

#include "tft_board.h"
#include "esp_check.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "freenove_keys";
static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS >= 2, "Freenove must detect multiple touches");
esp_lcd_panel_io_handle_t g_touch_io;
esp_lcd_touch_handle_t g_touch;
bool g_wait_for_release = true;
int g_press_source;
}

esp_err_t freenove_keys_init() {
    if (g_touch) return ESP_OK;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "touch I2C0");
    // Keep FT5x06 defaults without its SDK-version-dependent C++ field order.
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    io.control_phase_bytes = 1; io.dc_bit_offset = 0; io.lcd_cmd_bits = 8;
    io.flags.disable_control_phase = 1;
    io.scl_speed_hz = 400000;
    io.transaction_timeout_ms = 50;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &io, &g_touch_io), TAG, "touch IO");
    esp_lcd_touch_config_t touch = {};
    // Presence only: no coordinate transform or LVGL pointer is registered.
    // Keep reset/interrupt unused as in the pinned owning board class.
    touch.x_max = 240;
    touch.y_max = 320;
    touch.rst_gpio_num = GPIO_NUM_NC;
    touch.int_gpio_num = GPIO_NUM_NC;
    const esp_err_t result = esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch);
    if (result != ESP_OK) {
        const esp_err_t cleanup = esp_lcd_panel_io_del(g_touch_io);
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "touch_io_cleanup failed: %s", esp_err_to_name(cleanup));
        g_touch_io = nullptr;
        ESP_LOGE(TAG, "touch_init failed: %s", esp_err_to_name(result));
    }
    return result;
}

ManualInputKey freenove_keys_read() {
    if (!g_touch) return ManualInputKey::Invalid;
    const esp_err_t read_result = esp_lcd_touch_read_data(g_touch);
    esp_lcd_touch_point_data_t points[2]{};
    uint8_t count = 0;
    // Consume the driver cache even on an error; an old point must never
    // become a later press or a successful release after an I2C failure.
    const esp_err_t data_result = esp_lcd_touch_get_data(g_touch, points, &count, 2);
    if (read_result != ESP_OK || data_result != ESP_OK) {
        g_wait_for_release = true;
        g_press_source = 0;
        return ManualInputKey::Invalid;
    }
    const bool boot = !gpio_get_level(GPIO_NUM_0);
    if (!count && !boot) {
        g_wait_for_release = false;
        g_press_source = 0;
        return ManualInputKey::None;
    }
    const int source = boot ? 1 : 2;
    if (count > 1 || (boot && count) || (g_press_source && source != g_press_source)) {
        g_wait_for_release = true;
    }
    if (g_wait_for_release) return ManualInputKey::Invalid;
    g_press_source = source;
    return ManualInputKey::Listen;
}
