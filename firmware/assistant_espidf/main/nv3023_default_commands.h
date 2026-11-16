// SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// NV3023 1.0.1 default data modified by MakerM0, fixed source commit
// 15dae9531b4fc031bcc12dea1ddfe02517ca7711; third_party/nv3023/LICENSE.
#pragma once
#include <stdint.h>
struct Nv3023DefaultCommand { uint8_t code; uint8_t data[16]; uint8_t length; uint16_t delay_ms; };
inline constexpr Nv3023DefaultCommand NV3023_DEFAULT_COMMANDS[] = {
    {0xFF, {0xA5}, 1, 0},
    {0x3E, {0x09}, 1, 0},
    {0x3A, {0x05}, 1, 0},
    {0x82, {0x00}, 1, 0},
    {0x98, {0x00}, 1, 0},
    {0x63, {0x0F}, 1, 0},
    {0x64, {0x0F}, 1, 0},
    {0xB4, {0x34}, 1, 0},
    {0xB5, {0x30}, 1, 0},
    {0x83, {0x03}, 1, 0},
    {0x86, {0x04}, 1, 0},
    {0x87, {0x16}, 1, 0},
    {0x88, {0x0A}, 1, 0},
    {0x89, {0x27}, 1, 0},
    {0x93, {0x63}, 1, 0},
    {0x96, {0x81}, 1, 0},
    {0xC3, {0x10}, 1, 0},
    {0xE6, {0x00}, 1, 0},
    {0x99, {0x01}, 1, 0},
    {0x70, {0x09, 0x1D, 0x14, 0x0A, 0x11, 0x16, 0x38, 0x0B, 0x08, 0x3E, 0x07, 0x0D, 0x16, 0x0F, 0x14, 0x05}, 16, 0},
    {0xA0, {0x04, 0x28, 0x0C, 0x11, 0x0B, 0x23, 0x45, 0x07, 0x0A, 0x3B, 0x0D, 0x18, 0x14, 0x0F, 0x19, 0x08}, 16, 0},
    {0xFF, {0x00}, 1, 0},
    {0x11, {0x00}, 0, 200},
    {0x29, {0x00}, 0, 10},
    {0x2C, {0x00}, 0, 0},
};
static_assert([] {
    for (const auto &command : NV3023_DEFAULT_COMMANDS) {
        if (command.length > sizeof(command.data) || command.code == 0x36) return false;
        if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x05)) return false;
    }
    return true;
}(), "NV3023 default commands must fit their buffers and retain the reviewed format");
