#pragma once
#include "sh8601_rgb565.h"

inline constexpr uint8_t sh8601_206_payload_1[] = {0x80};
inline constexpr uint8_t sh8601_206_payload_2[] = {0x1, 0xd1};
inline constexpr uint8_t sh8601_206_payload_3[] = {0x0};
inline constexpr uint8_t sh8601_206_payload_4[] = {0x20};
inline constexpr uint8_t sh8601_206_payload_5[] = {0xff};
inline constexpr uint8_t sh8601_206_payload_6[] = {0x0};
inline constexpr uint8_t sh8601_206_payload_7[] = {0x0, 0x16, 0x1, 0xaf};
inline constexpr uint8_t sh8601_206_payload_8[] = {0x0, 0x0, 0x1, 0xf5};
inline constexpr uint8_t sh8601_206_payload_10[] = {0x0};
inline const Sh8601Command SH8601_206_COMMANDS[] = {
    {0x11, nullptr, 0, 120},
    {0xc4, sh8601_206_payload_1, 1, 0},
    {0x44, sh8601_206_payload_2, 2, 0},
    {0x35, sh8601_206_payload_3, 1, 0},
    {0x53, sh8601_206_payload_4, 1, 10},
    {0x63, sh8601_206_payload_5, 1, 10},
    {0x51, sh8601_206_payload_6, 1, 10},
    {0x2a, sh8601_206_payload_7, 4, 0},
    {0x2b, sh8601_206_payload_8, 4, 0},
    {0x29, nullptr, 0, 10},
    {0x51, sh8601_206_payload_10, 1, 0},
};
