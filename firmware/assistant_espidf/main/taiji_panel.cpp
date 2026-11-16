// SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// QSPI lifecycle adapted from esp-iot-solution@91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f.
// License: third_party/st77916/LICENSE. Board data retains its separate MIT notice.
#include "taiji_panel.h"
#include "st77916_qspi_panel.h"

#include <array>
#include "driver/gpio.h"
#include "taiji_panel_init.h"

namespace {
#if defined(ASSISTANT_BOARD_TAIJI_PDM)
constexpr const auto &kCommands = TAIJI_PDM_PANEL;
#else
constexpr const auto &kCommands = TAIJI_STD_PANEL;
#endif
static_assert([] {
    bool user_bank = true;
    for (const auto &command : kCommands) {
        if (command.length > sizeof(command.data)) return false;
        if (user_bank && command.code == 0x36 && command.length != 1) return false;
        if (user_bank && command.code == 0x3a && (command.length != 1 || command.data[0] != 0x55)) return false;
        if (command.code == 0xf0) {
            if (command.length != 1) return false;
            user_bank = command.data[0] == 0;
        }
    }
    return user_bank;
}(), "Taiji table must end in user bank and preserve RGB565 drawing");

}

esp_err_t taiji_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *handle) {
    static std::array<St77916QspiCommand, sizeof(kCommands) / sizeof(kCommands[0])> commands{};
    for (size_t index = 0; index < commands.size(); ++index) {
        const auto &command = kCommands[index];
        commands[index] = {command.code, command.data, command.length, command.delay_ms};
    }
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = GPIO_NUM_47;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    config.bits_per_pixel = 16;
    return st77916_qspi_panel_create(io, &config, commands.data(), commands.size(), handle);
}
