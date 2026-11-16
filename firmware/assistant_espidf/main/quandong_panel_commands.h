// Command data adapted from XiaoZhi v2.5.2 commit
// 49b4380b3faa0ceadd28a3e78aaf0f311c46c57e, main/boards/quandong-s3-dev/quandong_s3_dev_board.cc.
// Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c) 2025 Project Contributors.
// MIT notice: third_party/xiaozhi_quandong/LICENSE.
#pragma once
#include <stdint.h>

// The source has three cmd0 rows backed by one byte but declaring17/17/41
// bytes. They are not sent or reinterpreted as commands. ILI9341 2.1.0
// already sends SLPOUT and waits100ms; common startup calls display-on.
struct QuandongPanelCommand { uint8_t code; uint8_t data[15]; uint8_t length; };
inline constexpr QuandongPanelCommand QUANDONG_PANEL_COMMANDS[] = {
    {0xCF, {0x00, 0xC1, 0x30}, 3},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
    {0xE8, {0x85, 0x00, 0x78}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x13}, 1},
    {0xC1, {0x13}, 1},
    {0xC5, {0x22, 0x35}, 2},
    {0xC7, {0xBD}, 1},
    {0x36, {0x08}, 1},
    {0xB6, {0x0A, 0xA2}, 2},
    {0x3A, {0x55}, 1},
    {0xF6, {0x01, 0x30}, 2},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x00}, 1},
    {0x26, {0x01}, 1},
    {0xE0, {0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 15},
    {0xE1, {0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 15},
};
static_assert([] {
    for (const auto &command : QUANDONG_PANEL_COMMANDS) {
        if (command.length > sizeof(command.data)) return false;
    }
    return true;
}(), "Quandong panel command exceeds its backing data");
