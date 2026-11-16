// Command/waveform data adapted from XiaoZhi v2.5.2 at
// 49b4380b3faa0ceadd28a3e78aaf0f311c46c57e, main/display/epd_display/.
// Copyright (c)2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c)2025 Project Contributors.
// MIT notice: third_party/xiaozhi_maza/LICENSE.
#pragma once
#include <stdint.h>

inline constexpr uint8_t MAZA_FULL_WAVEFORM[] = {
    0x50, 0xaa, 0x55, 0xaa, 0x11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0xff, 0xff, 0x1f, 0, 0, 0, 0, 0, 0, 0,
};
inline constexpr uint8_t MAZA_PARTIAL_WAVEFORM[] = {
    0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0x13, 0x14, 0x44, 0x12, 0, 0, 0, 0, 0, 0,
};
struct PaperCommand { uint8_t code; uint8_t data[4]; uint8_t length; };
inline constexpr PaperCommand MAZA_SETUP[] = {
    {0x01, {0x27, 0x01, 0}, 3}, {0x0c, {0xd7, 0xd6, 0x9d}, 3},
    {0x2c, {0x9a}, 1}, {0x3a, {0x1a}, 1}, {0x3b, {0x08}, 1},
    {0x11, {0x03}, 1}, {0x3c, {0x33}, 1},
};
