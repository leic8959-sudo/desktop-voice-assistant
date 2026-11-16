// SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// QSPI lifecycle adapted from esp-iot-solution@e8ae944284047eb1a3a89e2538dd35f487396fa6.
// License: third_party/axs15231b/LICENSE. Board tables retain their own notices.
#include "axs_qspi_panel.h"

#include <new>
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "axs_qspi_panel";
struct Panel {
    esp_lcd_panel_t interface{};
    esp_lcd_panel_io_handle_t io = nullptr;
    const AxsQspiCommand *commands = nullptr;
    size_t command_count = 0;
    int width = 0, height = 0;
    uint8_t madctl = 0, colmod = 0x55;
};
Panel *state(esp_lcd_panel_t *handle) { return reinterpret_cast<Panel *>(handle); }

esp_err_t write_parameter(Panel *panel, uint8_t command, const void *data = nullptr, size_t bytes = 0) {
    return esp_lcd_panel_io_tx_param(panel->io, (0x02u << 24) | (static_cast<uint32_t>(command) << 8), data, bytes);
}

esp_err_t panel_reset(esp_lcd_panel_t *handle) {
    ESP_RETURN_ON_ERROR(write_parameter(state(handle), 0x01), TAG, "software reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t panel_initialize(esp_lcd_panel_t *handle) {
    Panel *panel = state(handle);
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x11), TAG, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &panel->madctl, 1), TAG, "initial MADCTL");
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x3a, &panel->colmod, 1), TAG, "initial COLMOD");
    for (size_t index = 0; index < panel->command_count; ++index) {
        const auto &command = panel->commands[index];
        ESP_RETURN_ON_ERROR(write_parameter(panel, command.code, command.data, command.length), TAG, "init command0x%02x", command.code);
        if (command.code == 0x36) panel->madctl = command.data[0];
        if (command.code == 0x3a) panel->colmod = command.data[0];
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}

esp_err_t panel_delete(esp_lcd_panel_t *handle) { delete state(handle); return ESP_OK; }

esp_err_t panel_draw(esp_lcd_panel_t *handle, int x_start, int y_start, int x_end, int y_end, const void *pixels) {
    Panel *panel = state(handle);
    if (!pixels || x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start || x_end > panel->width || y_end > panel->height) return ESP_ERR_INVALID_ARG;
    const uint8_t columns[] = {static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start), static_cast<uint8_t>((x_end - 1) >> 8), static_cast<uint8_t>(x_end - 1)};
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x2a, columns, sizeof(columns)), TAG, "column window");
    // The reviewed QSPI driver does not send RASET. A row0 write starts a
    // native frame; later rows continue it. The board's full-frame flush
    // owns stripe order, conversion, completion waits and DMA memory lifetime.
    const uint32_t command = (0x32u << 24) | ((y_start == 0 ? 0x2cu : 0x3cu) << 8);
    const size_t bytes = static_cast<size_t>(x_end - x_start) * (y_end - y_start) * sizeof(uint16_t);
    return esp_lcd_panel_io_tx_color(panel->io, command, pixels, bytes);
}

esp_err_t panel_mirror(esp_lcd_panel_t *handle, bool mirror_x, bool mirror_y) {
    // Existing frame conversion requires native axes. Arbitrary hardware
    // transforms are not exposed by these three fixed board configurations.
    if (mirror_x || mirror_y) return ESP_ERR_NOT_SUPPORTED;
    Panel *panel = state(handle);
    const uint8_t value = panel->madctl & ~0xc0;
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &value, 1), TAG, "native mirror axes");
    panel->madctl = value;
    return ESP_OK;
}

esp_err_t panel_swap(esp_lcd_panel_t *handle, bool swapped) {
    if (swapped) return ESP_ERR_NOT_SUPPORTED;
    Panel *panel = state(handle);
    const uint8_t value = panel->madctl & ~0x20;
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &value, 1), TAG, "native swap axes");
    panel->madctl = value;
    return ESP_OK;
}

esp_err_t panel_gap(esp_lcd_panel_t *, int x, int y) { return x == 0 && y == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED; }
esp_err_t panel_invert(esp_lcd_panel_t *handle, bool inverted) { return write_parameter(state(handle), inverted ? 0x21 : 0x20); }
esp_err_t panel_enable(esp_lcd_panel_t *handle, bool enabled) { return write_parameter(state(handle), enabled ? 0x29 : 0x28); }
}

esp_err_t axs_qspi_panel_create(esp_lcd_panel_io_handle_t io, int width, int height,
    const AxsQspiCommand *commands, size_t command_count, esp_lcd_panel_handle_t *handle) {
    if (!io || !handle || !commands || !command_count || command_count > 64) return ESP_ERR_INVALID_ARG;
    *handle = nullptr;
    if (!((width == 320 && height == 480) || (width == 172 && height == 640))) return ESP_ERR_NOT_SUPPORTED;
    for (size_t index = 0; index < command_count; ++index) {
        const auto &command = commands[index];
        if (command.length > 32 || (command.length && !command.data)) return ESP_ERR_INVALID_ARG;
        if (command.code == 0x36 && command.length != 1) return ESP_ERR_NOT_SUPPORTED;
        if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x55)) return ESP_ERR_NOT_SUPPORTED;
    }
    Panel *panel = new (std::nothrow) Panel{};
    if (!panel) return ESP_ERR_NO_MEM;
    panel->io = io;
    panel->width = width; panel->height = height;
    panel->commands = commands; panel->command_count = command_count;
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
