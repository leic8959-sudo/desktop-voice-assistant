// Native panel lifecycle for the pinned JD9853 172x320 initialization data.
#include "jd9853_panel.h"

#include <new>
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jd9853_panel_commands.h"

namespace {
constexpr const char *TAG = "jd9853_panel";
constexpr int kWidth = 172, kHeight = 320;
struct Panel {
    esp_lcd_panel_t interface{};
    esp_lcd_panel_io_handle_t io = nullptr;
    uint8_t madctl = 0;
    int x_gap = 0, y_gap = 0;
};
Panel *state(esp_lcd_panel_t *panel) { return reinterpret_cast<Panel *>(panel); }

esp_err_t write_command(Panel *panel, int command) {
    return esp_lcd_panel_io_tx_param(panel->io, command, nullptr, 0);
}

esp_err_t panel_reset(esp_lcd_panel_t *handle) {
    ESP_RETURN_ON_ERROR(write_command(state(handle), 0x01), TAG, "software reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t panel_initialize(esp_lcd_panel_t *handle) {
    Panel *panel = state(handle);
    ESP_RETURN_ON_ERROR(write_command(panel, 0x11), TAG, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x00, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io, 0x36, &axes, 1), TAG, "initial MADCTL");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io, 0x3a, &format, 1), TAG, "initial COLMOD");
    panel->madctl = axes;
    for (const auto &command : JD9853_COMMANDS) {
        const esp_err_t result = esp_lcd_panel_io_tx_param(panel->io, command.code,
            command.length ? command.data : nullptr, command.length);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "initialize_failed command=0x%02x error=%s", command.code, esp_err_to_name(result));
            return result;
        }
        if (command.code == 0x36) panel->madctl = command.data[0];
        if (command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    ESP_LOGI(TAG, "initialized pinned_172x320_sequence");
    return ESP_OK;
}

esp_err_t panel_delete(esp_lcd_panel_t *panel) {
    delete state(panel);
    return ESP_OK;
}

esp_err_t panel_draw(esp_lcd_panel_t *handle, int x_start, int y_start, int x_end, int y_end, const void *pixels) {
    Panel *panel = state(handle);
    const bool swapped = (panel->madctl & 0x20) != 0;
    const int width = swapped ? kHeight : kWidth, height = swapped ? kWidth : kHeight;
    if (!pixels || x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start || x_end > width || y_end > height) return ESP_ERR_INVALID_ARG;
    x_start += panel->x_gap; x_end += panel->x_gap;
    y_start += panel->y_gap; y_end += panel->y_gap;
    if (x_end > (swapped ? 320 : 240) || y_end > (swapped ? 240 : 320)) return ESP_ERR_INVALID_ARG;
    const uint8_t columns[] = {static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start), static_cast<uint8_t>((x_end - 1) >> 8), static_cast<uint8_t>(x_end - 1)};
    const uint8_t rows[] = {static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start), static_cast<uint8_t>((y_end - 1) >> 8), static_cast<uint8_t>(y_end - 1)};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io, 0x2a, columns, sizeof(columns)), TAG, "column window");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io, 0x2b, rows, sizeof(rows)), TAG, "row window");
    const size_t bytes = static_cast<size_t>(x_end - x_start) * (y_end - y_start) * sizeof(uint16_t);
    return esp_lcd_panel_io_tx_color(panel->io, 0x2c, pixels, bytes);
}

esp_err_t set_madctl(Panel *panel, uint8_t value) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io, 0x36, &value, sizeof(value)), TAG, "MADCTL");
    panel->madctl = value;
    return ESP_OK;
}

esp_err_t panel_mirror(esp_lcd_panel_t *handle, bool mirror_x, bool mirror_y) {
    Panel *panel = state(handle);
    return set_madctl(panel, (panel->madctl & ~0xc0) | (mirror_x ? 0x40 : 0) | (mirror_y ? 0x80 : 0));
}

esp_err_t panel_swap(esp_lcd_panel_t *handle, bool swapped) {
    Panel *panel = state(handle);
    return set_madctl(panel, swapped ? panel->madctl | 0x20 : panel->madctl & ~0x20);
}

esp_err_t panel_gap(esp_lcd_panel_t *handle, int x, int y) {
    if (x < 0 || y < 0 || x > 34 || y > 34) return ESP_ERR_NOT_SUPPORTED;
    state(handle)->x_gap = x; state(handle)->y_gap = y;
    return ESP_OK;
}

esp_err_t panel_invert(esp_lcd_panel_t *panel, bool inverted) { return write_command(state(panel), inverted ? 0x21 : 0x20); }
esp_err_t panel_enable(esp_lcd_panel_t *panel, bool enabled) { return write_command(state(panel), enabled ? 0x29 : 0x28); }
esp_err_t panel_sleep(esp_lcd_panel_t *panel, bool sleeping) {
    ESP_RETURN_ON_ERROR(write_command(state(panel), sleeping ? 0x10 : 0x11), TAG, "sleep");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}
}

esp_err_t jd9853_panel_create(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *config, esp_lcd_panel_handle_t *handle) {
    if (!io || !config || !handle) return ESP_ERR_INVALID_ARG;
    *handle = nullptr;
    if (config->reset_gpio_num != -1 || config->bits_per_pixel != 16 || config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_RGB || config->vendor_config) return ESP_ERR_NOT_SUPPORTED;
    Panel *panel = new (std::nothrow) Panel{};
    if (!panel) return ESP_ERR_NO_MEM;
    panel->io = io;
    panel->interface.reset = panel_reset;
    panel->interface.init = panel_initialize;
    panel->interface.del = panel_delete;
    panel->interface.draw_bitmap = panel_draw;
    panel->interface.mirror = panel_mirror;
    panel->interface.swap_xy = panel_swap;
    panel->interface.set_gap = panel_gap;
    panel->interface.invert_color = panel_invert;
    panel->interface.disp_on_off = panel_enable;
    panel->interface.disp_sleep = panel_sleep;
    *handle = &panel->interface;
    return ESP_OK;
}
