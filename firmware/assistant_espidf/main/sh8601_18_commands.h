#pragma once
#include "sh8601_rgb565.h"

inline constexpr uint8_t sh8601_18_payload_1[] = {0x1, 0xd1};
inline constexpr uint8_t sh8601_18_payload_2[] = {0x0};
inline constexpr uint8_t sh8601_18_payload_3[] = {0x20};
inline constexpr uint8_t sh8601_18_payload_4[] = {0x0, 0x0, 0x1, 0x6f};
inline constexpr uint8_t sh8601_18_payload_5[] = {0x0, 0x0, 0x1, 0xbf};
inline constexpr uint8_t sh8601_18_payload_6[] = {0x0};
inline constexpr uint8_t sh8601_18_payload_8[] = {0x0};
inline const Sh8601Command SH8601_18_COMMANDS[] = {
    {0x11, nullptr, 0, 120},
    {0x44, sh8601_18_payload_1, 2, 0},
    {0x35, sh8601_18_payload_2, 1, 0},
    {0x53, sh8601_18_payload_3, 1, 10},
    {0x2a, sh8601_18_payload_4, 4, 0},
    {0x2b, sh8601_18_payload_5, 4, 0},
    {0x51, sh8601_18_payload_6, 1, 10},
    {0x29, nullptr, 0, 10},
    {0x51, sh8601_18_payload_8, 1, 0},
};
