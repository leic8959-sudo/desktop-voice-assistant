// SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// ST7796 1.4.0 default data from esp-bsp@5a39b90736dad653869d101f069a06758082a39e.
// The reviewed1.3.5 SPI table at618b05297dae1aa3fab986d5ca7b89b328e1919a is identical.
// License: third_party/st7796/LICENSE.
#pragma once
#include <stdint.h>
struct St7796DefaultCommand { uint8_t code; uint8_t data[14]; uint8_t length; };
inline constexpr St7796DefaultCommand ST7796_DEFAULT_COMMANDS[] = {
    {0xF0, {0xC3}, 1},
    {0xF0, {0x96}, 1},
    {0xB4, {0x01}, 1},
    {0xB7, {0xC6}, 1},
    {0xE8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8},
    {0xC1, {0x06}, 1},
    {0xC2, {0xA7}, 1},
    {0xC5, {0x18}, 1},
    {0xE0, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 14},
    {0xE1, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2D, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 14},
    {0xF0, {0x3C}, 1},
    {0xF0, {0x69}, 1},
};
static_assert([] {
    for (const auto &command : ST7796_DEFAULT_COMMANDS) {
        if (command.length > sizeof(command.data) || command.code == 0x36 || command.code == 0x3a) return false;
    }
    return true;
}(), "ST7796 defaults must preserve the reviewed axes and pixel format");
