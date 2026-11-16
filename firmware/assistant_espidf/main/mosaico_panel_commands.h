#pragma once
#include <cstdint>
#include "esp_lcd_co5300.h"

// Derived from fixed ESP-Mosaico CO5300 table at XiaoZhi commit
// 49b4380b3faa0ceadd28a3e78aaf0f311c46c57e.
// MIT notice: third_party/xiaozhi_p4_panels/LICENSE.
// Startup brightness51 is zero until the application UI is ready.

inline constexpr uint8_t kMosaicoData1[] = {0x20};
inline constexpr uint8_t kMosaicoData2[] = {0x10};
inline constexpr uint8_t kMosaicoData3[] = {0xA0};
inline constexpr uint8_t kMosaicoData4[] = {0x00};
inline constexpr uint8_t kMosaicoData5[] = {0x80};
inline constexpr uint8_t kMosaicoData6[] = {0x55};
inline constexpr uint8_t kMosaicoData7[] = {0x00};
inline constexpr uint8_t kMosaicoData8[] = {0x20};
inline constexpr uint8_t kMosaicoData9[] = {0x00};
inline constexpr uint8_t kMosaicoData10[] = {0xFF};
inline constexpr uint8_t kMosaicoData11[] = {0x00, 0x00, 0x01, 0xDF};
inline constexpr uint8_t kMosaicoData12[] = {0x00, 0x00, 0x01, 0xDF};
inline constexpr co5300_lcd_init_cmd_t mosaico_panel_commands[] = {
    {0x11, nullptr, 0, 600},
    {0xFE, kMosaicoData1, 1, 0},
    {0x19, kMosaicoData2, 1, 0},
    {0x1C, kMosaicoData3, 1, 0},
    {0xFE, kMosaicoData4, 1, 0},
    {0xC4, kMosaicoData5, 1, 0},
    {0x3A, kMosaicoData6, 1, 0},
    {0x35, kMosaicoData7, 1, 0},
    {0x53, kMosaicoData8, 1, 0},
    {0x51, kMosaicoData9, 1, 0},
    {0x63, kMosaicoData10, 1, 0},
    {0x2A, kMosaicoData11, 4, 0},
    {0x2B, kMosaicoData12, 4, 0},
    {0x29, nullptr, 0, 600},
};
