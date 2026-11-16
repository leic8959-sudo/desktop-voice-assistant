#pragma once
#include <cstdint>
#include "esp_lcd_st7701.h"

// Command bytes/counts/delays derived from the fixed upstream 4.3/7 panel branch.
// Source: 78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e
// See main/third_party/xiaozhi_p4_panels/LICENSE.

inline constexpr uint8_t kST7701Data0[] = {0x77, 0x01, 0x00, 0x00, 0x13};
inline constexpr uint8_t kST7701Data1[] = {0x08};
inline constexpr uint8_t kST7701Data2[] = {0x77, 0x01, 0x00, 0x00, 0x10};
inline constexpr uint8_t kST7701Data3[] = {0x63, 0x00};
inline constexpr uint8_t kST7701Data4[] = {0x0D, 0x02};
inline constexpr uint8_t kST7701Data5[] = {0x17, 0x08};
inline constexpr uint8_t kST7701Data6[] = {0x10};
inline constexpr uint8_t kST7701Data7[] = {0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69};
inline constexpr uint8_t kST7701Data8[] = {0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55};
inline constexpr uint8_t kST7701Data9[] = {0x77, 0x01, 0x00, 0x00, 0x11};
inline constexpr uint8_t kST7701Data10[] = {0x5D};
inline constexpr uint8_t kST7701Data11[] = {0x4E};
inline constexpr uint8_t kST7701Data12[] = {0x87};
inline constexpr uint8_t kST7701Data13[] = {0x80};
inline constexpr uint8_t kST7701Data14[] = {0x4E};
inline constexpr uint8_t kST7701Data15[] = {0x85};
inline constexpr uint8_t kST7701Data16[] = {0x21};
inline constexpr uint8_t kST7701Data17[] = {0x10, 0x1F};
inline constexpr uint8_t kST7701Data18[] = {0x03};
inline constexpr uint8_t kST7701Data19[] = {0x00};
inline constexpr uint8_t kST7701Data20[] = {0x78};
inline constexpr uint8_t kST7701Data21[] = {0x78};
inline constexpr uint8_t kST7701Data22[] = {0x88};
inline constexpr uint8_t kST7701Data23[] = {0x00, 0x3A, 0x02};
inline constexpr uint8_t kST7701Data24[] = {0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40};
inline constexpr uint8_t kST7701Data25[] = {0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00};
inline constexpr uint8_t kST7701Data26[] = {0x00, 0x00, 0x33, 0x33};
inline constexpr uint8_t kST7701Data27[] = {0x44, 0x44};
inline constexpr uint8_t kST7701Data28[] = {0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0};
inline constexpr uint8_t kST7701Data29[] = {0x00, 0x00, 0x33, 0x33};
inline constexpr uint8_t kST7701Data30[] = {0x44, 0x44};
inline constexpr uint8_t kST7701Data31[] = {0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0};
inline constexpr uint8_t kST7701Data32[] = {0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00};
inline constexpr uint8_t kST7701Data33[] = {0x08, 0x01};
inline constexpr uint8_t kST7701Data34[] = {0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B};
inline constexpr uint8_t kST7701Data35[] = {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54};
inline constexpr uint8_t kST7701Data36[] = {0x77, 0x01, 0x00, 0x00, 0x00};
inline constexpr uint8_t kST7701Data37[] = {0x00};
inline constexpr uint8_t kST7701Data38[] = {0x00};

inline const st7701_lcd_init_cmd_t waveshare_p4_st7701_init_cmds[] = {
    {0xFF, kST7701Data0, 5, 0},
    {0xEF, kST7701Data1, 1, 0},
    {0xFF, kST7701Data2, 5, 0},
    {0xC0, kST7701Data3, 2, 0},
    {0xC1, kST7701Data4, 2, 0},
    {0xC2, kST7701Data5, 2, 0},
    {0xCC, kST7701Data6, 1, 0},
    {0xB0, kST7701Data7, 16, 0},
    {0xB1, kST7701Data8, 16, 0},
    {0xFF, kST7701Data9, 5, 0},
    {0xB0, kST7701Data10, 1, 0},
    {0xB1, kST7701Data11, 1, 0},
    {0xB2, kST7701Data12, 1, 0},
    {0xB3, kST7701Data13, 1, 0},
    {0xB5, kST7701Data14, 1, 0},
    {0xB7, kST7701Data15, 1, 0},
    {0xB8, kST7701Data16, 1, 0},
    {0xB9, kST7701Data17, 2, 0},
    {0xBB, kST7701Data18, 1, 0},
    {0xBC, kST7701Data19, 1, 0},
    {0xC1, kST7701Data20, 1, 0},
    {0xC2, kST7701Data21, 1, 0},
    {0xD0, kST7701Data22, 1, 0},
    {0xE0, kST7701Data23, 3, 0},
    {0xE1, kST7701Data24, 11, 0},
    {0xE2, kST7701Data25, 13, 0},
    {0xE3, kST7701Data26, 4, 0},
    {0xE4, kST7701Data27, 2, 0},
    {0xE5, kST7701Data28, 16, 0},
    {0xE6, kST7701Data29, 4, 0},
    {0xE7, kST7701Data30, 2, 0},
    {0xE8, kST7701Data31, 16, 0},
    {0xEB, kST7701Data32, 7, 0},
    {0xEC, kST7701Data33, 2, 0},
    {0xED, kST7701Data34, 16, 0},
    {0xEF, kST7701Data35, 6, 0},
    {0xFF, kST7701Data36, 5, 0},
    {0x11, kST7701Data37, 0, 120},
    {0x29, kST7701Data38, 0, 0},
};
