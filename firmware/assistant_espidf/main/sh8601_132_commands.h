#pragma once
#include "sh8601_rgb565.h"

inline constexpr uint8_t sh8601_132_payload_0[] = {0x0};
inline constexpr uint8_t sh8601_132_payload_1[] = {0x80};
inline constexpr uint8_t sh8601_132_payload_2[] = {0x55};
inline constexpr uint8_t sh8601_132_payload_3[] = {0x0};
inline constexpr uint8_t sh8601_132_payload_4[] = {0x20};
inline constexpr uint8_t sh8601_132_payload_5[] = {0x0};
inline constexpr uint8_t sh8601_132_payload_6[] = {0xff};
inline constexpr uint8_t sh8601_132_payload_7[] = {0x0, 0x6, 0x1, 0xd7};
inline constexpr uint8_t sh8601_132_payload_8[] = {0x0, 0x0, 0x1, 0xd1};
inline const Sh8601Command SH8601_132_COMMANDS[] = {
    {0xfe, sh8601_132_payload_0, 1, 0},
    {0xc4, sh8601_132_payload_1, 1, 0},
    {0x3a, sh8601_132_payload_2, 1, 0},
    {0x35, sh8601_132_payload_3, 1, 0},
    {0x53, sh8601_132_payload_4, 1, 0},
    {0x51, sh8601_132_payload_5, 1, 0},
    {0x63, sh8601_132_payload_6, 1, 0},
    {0x2a, sh8601_132_payload_7, 4, 0},
    {0x2b, sh8601_132_payload_8, 4, 0},
    {0x11, nullptr, 0, 100},
    {0x29, nullptr, 0, 0},
};
