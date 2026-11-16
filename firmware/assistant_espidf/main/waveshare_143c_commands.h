// Vendor command data adapted from XiaoZhi v2.5.2 commit
// 49b4380b3faa0ceadd28a3e78aaf0f311c46c57e, Waveshare1.43C board source.
// Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c) 2025 Project Contributors
// SPDX-License-Identifier: MIT
// Modified: fixed-size C++ data;51brightness starts at0 instead ofFF.
// Original terms and source notice: third_party/xiaozhi_ws143c/.
#pragma once
#include <stdint.h>

struct Waveshare143cCommand { uint8_t code; uint8_t data[4]; uint8_t length; uint16_t delay_ms; };
inline constexpr Waveshare143cCommand WAVESHARE_143C_COMMANDS[] = {
    {0xFE, {0x00}, 1, 0},
    {0xC4, {0x80}, 1, 0},
    {0x3A, {0x55}, 1, 0},
    {0x35, {0x00}, 1, 0},
    {0x53, {0x20}, 1, 0},
    {0x51, {0x00}, 1, 0},
    {0x36, {0xC0}, 1, 0},
    {0x63, {0xFF}, 1, 0},
    {0x2A, {0x00,0x06,0x01,0xD7}, 4, 0},
    {0x2B, {0x00,0x00,0x01,0xD1}, 4, 0},
    {0x11, {0x00}, 0, 100},
    {0x29, {0x00}, 0, 0},
};
