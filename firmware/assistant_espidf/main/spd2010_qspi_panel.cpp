// SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// SPD2010 lifecycle adapted from esp-iot-solution@12f6ca1182ec48889b17ec570fadaaf267cb336e.
// License and default table terms: third_party/spd2010/LICENSE.
#include "spd2010_qspi_panel.h"

#include <new>
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spd2010_default_commands.h"

namespace {
constexpr const char *TAG = "spd2010_qspi_panel";
constexpr int kWidth = 412, kHeight = 412;
static_assert([] {
    bool user_page = true;
    for (const auto &command : SPD2010_DEFAULT_COMMANDS) {
        if (command.length > sizeof(command.data)) return false;
        if (user_page && command.length && command.code == 0x3a && command.data[0] != 0x55) return false;
        if (command.code == 0xff && command.length > 2) user_page = command.data[2] == 0;
    }
    return user_page;
}(), "SPD2010 default sequence must preserve RGB565 and end in user page");
struct Panel {
    esp_lcd_panel_t interface{};
    esp_lcd_panel_io_handle_t io = nullptr;
    uint8_t madctl = 0;
};
Panel *state(esp_lcd_panel_t *handle) { return reinterpret_cast<Panel *>(handle); }

esp_err_t write_parameter(Panel *panel, uint8_t command, const void *data = nullptr, size_t bytes = 0) {
    return esp_lcd_panel_io_tx_param(panel->io, (0x02u << 24) | (static_cast<uint32_t>(command) << 8), data, bytes);
}

esp_err_t panel_reset(esp_lcd_panel_t *handle) {
    ESP_RETURN_ON_ERROR(write_parameter(state(handle), 0x01), TAG, "software reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t panel_initialize(esp_lcd_panel_t *handle) {
    Panel *panel = state(handle);
    const uint8_t user_page[] = {0x20, 0x10, 0x00};
    const uint8_t format = 0x55;
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0xff, user_page, sizeof(user_page)), TAG, "user command page");
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &panel->madctl, 1), TAG, "initial MADCTL");
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x3a, &format, 1), TAG, "initial COLMOD");
    bool user_page_selected = true;
    for (const auto &command : SPD2010_DEFAULT_COMMANDS) {
        ESP_RETURN_ON_ERROR(write_parameter(panel, command.code, command.data, command.length), TAG, "init command0x%02x", command.code);
        if (user_page_selected && command.length && command.code == 0x36) panel->madctl = command.data[0];
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
        // The fixed driver checks the third byte of FF to select user
        // commands. Preserve that page rule and every original delay.
        if (command.code == 0xff && command.length > 2) user_page_selected = command.data[2] == 0;
    }
    return ESP_OK;
}

esp_err_t panel_delete(esp_lcd_panel_t *handle) { delete state(handle); return ESP_OK; }

esp_err_t panel_draw(esp_lcd_panel_t *handle, int x_start, int y_start, int x_end, int y_end, const void *pixels) {
    if (!pixels || x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start || x_end > kWidth || y_end > kHeight) return ESP_ERR_INVALID_ARG;
    Panel *panel = state(handle);
    const uint8_t columns[] = {static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start), static_cast<uint8_t>((x_end - 1) >> 8), static_cast<uint8_t>(x_end - 1)};
    const uint8_t rows[] = {static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start), static_cast<uint8_t>((y_end - 1) >> 8), static_cast<uint8_t>(y_end - 1)};
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x2a, columns, sizeof(columns)), TAG, "column window");
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x2b, rows, sizeof(rows)), TAG, "row window");
    // The reviewed RGB565 path uses a16-bit pixel buffer throughout.
    const size_t bytes = static_cast<size_t>(x_end - x_start) * (y_end - y_start) * sizeof(uint16_t);
    return esp_lcd_panel_io_tx_color(panel->io, (0x32u << 24) | (0x2cu << 8), pixels, bytes);
}

esp_err_t panel_mirror(esp_lcd_panel_t *handle, bool mirror_x, bool mirror_y) {
    Panel *panel = state(handle);
    const uint8_t value = (panel->madctl & ~0x03) | (mirror_x ? 0x02 : 0) | (mirror_y ? 0x01 : 0);
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &value, 1), TAG, "MADCTL mirror");
    panel->madctl = value;
    return ESP_OK;
}

esp_err_t panel_swap(esp_lcd_panel_t *, bool) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t panel_gap(esp_lcd_panel_t *, int x, int y) { return x == 0 && y == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED; }
esp_err_t panel_invert(esp_lcd_panel_t *handle, bool inverted) { return write_parameter(state(handle), inverted ? 0x21 : 0x20); }
esp_err_t panel_enable(esp_lcd_panel_t *handle, bool enabled) { return write_parameter(state(handle), enabled ? 0x29 : 0x28); }
}

esp_err_t spd2010_qspi_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *handle) {
    if (!io || !handle) return ESP_ERR_INVALID_ARG;
    *handle = nullptr;
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
    *handle = &panel->interface;
    return ESP_OK;
}
