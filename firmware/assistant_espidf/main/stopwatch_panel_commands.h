// Vendor data adapted from78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e.
// Copyright (c)2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c)2025 Project Contributors. SPDX-License-Identifier: MIT
// Modified:51brightnessFF->00 for darkstartup;zero-length source entries retained.
// Original terms:third_party/xiaozhi_stopwatch/LICENSE.
#pragma once
#include <stdint.h>
struct StopwatchPanelCommand { uint8_t code; uint8_t data[4]; uint8_t length; uint16_t delay_ms; };
inline constexpr StopwatchPanelCommand WAVESHARE_143C_COMMANDS[] = {
    {0xFE, {0x00}, 0, 0},
    {0xC4, {0x80}, 1, 0},
    {0x3A, {0x55}, 0, 10},
    {0x35, {0x00}, 0, 10},
    {0x53, {0x20}, 1, 10},
    {0x51, {0x00}, 1, 10},
    {0x63, {0xFF}, 1, 10},
    {0x2A, {0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, {0x00}, 0, 120},
    {0x29, {0x00}, 0, 20},
};
