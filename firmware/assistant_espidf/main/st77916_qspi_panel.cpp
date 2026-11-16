// SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// QSPI lifecycle adapted from esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f.
// License: third_party/st77916/LICENSE. Board data retains its separate MIT notice.
#include "st77916_qspi_panel.h"

#include <new>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "st77916_qspi";
constexpr int kSize = 360;
struct Panel {
    esp_lcd_panel_t interface{};
    esp_lcd_panel_io_handle_t io = nullptr;
    uint8_t madctl = 0;
    gpio_num_t reset_pin = GPIO_NUM_NC;
    bool reset_active_high = false;
    const St77916QspiCommand *commands = nullptr;
    size_t command_count = 0;
};
Panel *state(esp_lcd_panel_t *handle) { return reinterpret_cast<Panel *>(handle); }

esp_err_t write_parameter(Panel *panel, uint8_t command, const void *data = nullptr, size_t bytes = 0) {
    return esp_lcd_panel_io_tx_param(panel->io, (0x02u << 24) | (static_cast<uint32_t>(command) << 8), data, bytes);
}

esp_err_t panel_reset(esp_lcd_panel_t *handle) {
    Panel *panel = state(handle);
    if (panel->reset_pin == GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(write_parameter(panel, 0x01), TAG, "software reset");
    } else {
        ESP_RETURN_ON_ERROR(gpio_set_level(panel->reset_pin, panel->reset_active_high ? 1 : 0), TAG, "reset assert");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(panel->reset_pin, panel->reset_active_high ? 0 : 1), TAG, "reset release");
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t panel_initialize(esp_lcd_panel_t *handle) {
    Panel *panel = state(handle);
    const uint8_t format = 0x55;
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &panel->madctl, 1), TAG, "initial MADCTL");
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x3a, &format, 1), TAG, "initial COLMOD");
    bool user_bank = true;
    for (size_t index = 0; index < panel->command_count; ++index) {
        const auto &command = panel->commands[index];
        ESP_RETURN_ON_ERROR(write_parameter(panel, command.code, command.data, command.length), TAG, "init command0x%02x", command.code);
        if (user_bank && command.code == 0x36) panel->madctl = command.data[0];
        if (command.code == 0xf0) user_bank = command.data[0] == 0;
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}

esp_err_t panel_delete(esp_lcd_panel_t *handle) {
    const gpio_num_t pin = state(handle)->reset_pin;
    const esp_err_t result = pin == GPIO_NUM_NC ? ESP_OK : gpio_reset_pin(pin);
    if (result != ESP_OK) ESP_LOGE(TAG, "release reset GPIO failed:%s", esp_err_to_name(result));
    delete state(handle);
    return result;
}

esp_err_t panel_draw(esp_lcd_panel_t *handle, int x_start, int y_start, int x_end, int y_end, const void *pixels) {
    if (!pixels || x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start || x_end > kSize || y_end > kSize) return ESP_ERR_INVALID_ARG;
    Panel *panel = state(handle);
    const uint8_t columns[] = {static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start), static_cast<uint8_t>((x_end - 1) >> 8), static_cast<uint8_t>(x_end - 1)};
    const uint8_t rows[] = {static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start), static_cast<uint8_t>((y_end - 1) >> 8), static_cast<uint8_t>(y_end - 1)};
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x2a, columns, sizeof(columns)), TAG, "column window");
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x2b, rows, sizeof(rows)), TAG, "row window");
    const size_t bytes = static_cast<size_t>(x_end - x_start) * (y_end - y_start) * sizeof(uint16_t);
    return esp_lcd_panel_io_tx_color(panel->io, (0x32u << 24) | (0x2cu << 8), pixels, bytes);
}

esp_err_t set_madctl(Panel *panel, uint8_t value) {
    ESP_RETURN_ON_ERROR(write_parameter(panel, 0x36, &value, 1), TAG, "MADCTL");
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

esp_err_t panel_gap(esp_lcd_panel_t *, int x, int y) { return x == 0 && y == 0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED; }
esp_err_t panel_invert(esp_lcd_panel_t *handle, bool inverted) { return write_parameter(state(handle), inverted ? 0x21 : 0x20); }
esp_err_t panel_enable(esp_lcd_panel_t *handle, bool enabled) { return write_parameter(state(handle), enabled ? 0x29 : 0x28); }
}

esp_err_t st77916_qspi_panel_create(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *config,
    const St77916QspiCommand *commands, size_t command_count, esp_lcd_panel_handle_t *handle) {
    if (!io || !config || !handle || !commands || !command_count || command_count > 256) return ESP_ERR_INVALID_ARG;
    *handle = nullptr;
    if (config->bits_per_pixel != 16 || config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_RGB ||
        config->vendor_config || (config->reset_gpio_num != GPIO_NUM_NC && !GPIO_IS_VALID_OUTPUT_GPIO(config->reset_gpio_num))) return ESP_ERR_NOT_SUPPORTED;
    bool user_bank = true;
    for (size_t index = 0; index < command_count; ++index) {
        const auto &command = commands[index];
        if (command.length > 14 || (command.length && !command.data)) return ESP_ERR_INVALID_ARG;
        if (user_bank && command.code == 0x36 && command.length != 1) return ESP_ERR_NOT_SUPPORTED;
        if (user_bank && command.code == 0x3a && (command.length != 1 || command.data[0] != 0x55)) return ESP_ERR_NOT_SUPPORTED;
        if (command.code == 0xf0) {
            if (command.length != 1) return ESP_ERR_NOT_SUPPORTED;
            user_bank = command.data[0] == 0;
        }
    }
    if (!user_bank) return ESP_ERR_NOT_SUPPORTED;
    Panel *panel = new (std::nothrow) Panel{};
    if (!panel) return ESP_ERR_NO_MEM;
    if (config->reset_gpio_num != GPIO_NUM_NC) {
        gpio_config_t reset = {};
        reset.pin_bit_mask = 1ULL << config->reset_gpio_num;
        reset.mode = GPIO_MODE_OUTPUT;
        const esp_err_t result = gpio_config(&reset);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "reset GPIO configure failed:%s", esp_err_to_name(result));
            delete panel;
            return result;
        }
    }
    panel->io = io;
    panel->reset_pin = static_cast<gpio_num_t>(config->reset_gpio_num);
    panel->reset_active_high = config->flags.reset_active_high;
    panel->commands = commands;
    panel->command_count = command_count;
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
