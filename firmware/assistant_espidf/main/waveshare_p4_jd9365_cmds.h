#pragma once
#include <cstdint>
#include "esp_lcd_jd9365.h"

// Derived from each exact v2.5.2 panel branch; retain its command order.
// 78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e
// MIT license: third_party/xiaozhi_p4_panels/LICENSE

#if defined(ASSISTANT_BOARD_P4_JD9365_34C)
inline constexpr uint8_t kJD9365Data0[] = {0x00};
inline constexpr uint8_t kJD9365Data1[] = {0x93};
inline constexpr uint8_t kJD9365Data2[] = {0x65};
inline constexpr uint8_t kJD9365Data3[] = {0xF8};
inline constexpr uint8_t kJD9365Data4[] = {0x01};
inline constexpr uint8_t kJD9365Data5[] = {0x01};
inline constexpr uint8_t kJD9365Data6[] = {0x00};
inline constexpr uint8_t kJD9365Data7[] = {0x41};
inline constexpr uint8_t kJD9365Data8[] = {0x10};
inline constexpr uint8_t kJD9365Data9[] = {0x44};
inline constexpr uint8_t kJD9365Data10[] = {0x00};
inline constexpr uint8_t kJD9365Data11[] = {0xD0};
inline constexpr uint8_t kJD9365Data12[] = {0x00};
inline constexpr uint8_t kJD9365Data13[] = {0x00};
inline constexpr uint8_t kJD9365Data14[] = {0xD0};
inline constexpr uint8_t kJD9365Data15[] = {0x00};
inline constexpr uint8_t kJD9365Data16[] = {0xFE};
inline constexpr uint8_t kJD9365Data17[] = {0x26};
inline constexpr uint8_t kJD9365Data18[] = {0x09};
inline constexpr uint8_t kJD9365Data19[] = {0x04};
inline constexpr uint8_t kJD9365Data20[] = {0x08};
inline constexpr uint8_t kJD9365Data21[] = {0x0A};
inline constexpr uint8_t kJD9365Data22[] = {0x78};
inline constexpr uint8_t kJD9365Data23[] = {0xFF};
inline constexpr uint8_t kJD9365Data24[] = {0xFF};
inline constexpr uint8_t kJD9365Data25[] = {0xFF};
inline constexpr uint8_t kJD9365Data26[] = {0x00};
inline constexpr uint8_t kJD9365Data27[] = {0x64};
inline constexpr uint8_t kJD9365Data28[] = {0xC7};
inline constexpr uint8_t kJD9365Data29[] = {0x18};
inline constexpr uint8_t kJD9365Data30[] = {0x0B};
inline constexpr uint8_t kJD9365Data31[] = {0x14};
inline constexpr uint8_t kJD9365Data32[] = {0x02};
inline constexpr uint8_t kJD9365Data33[] = {0x49};
inline constexpr uint8_t kJD9365Data34[] = {0x0A};
inline constexpr uint8_t kJD9365Data35[] = {0x1B};
inline constexpr uint8_t kJD9365Data36[] = {0x19};
inline constexpr uint8_t kJD9365Data37[] = {0x7F};
inline constexpr uint8_t kJD9365Data38[] = {0x56};
inline constexpr uint8_t kJD9365Data39[] = {0x43};
inline constexpr uint8_t kJD9365Data40[] = {0x37};
inline constexpr uint8_t kJD9365Data41[] = {0x33};
inline constexpr uint8_t kJD9365Data42[] = {0x25};
inline constexpr uint8_t kJD9365Data43[] = {0x2A};
inline constexpr uint8_t kJD9365Data44[] = {0x16};
inline constexpr uint8_t kJD9365Data45[] = {0x30};
inline constexpr uint8_t kJD9365Data46[] = {0x2F};
inline constexpr uint8_t kJD9365Data47[] = {0x32};
inline constexpr uint8_t kJD9365Data48[] = {0x53};
inline constexpr uint8_t kJD9365Data49[] = {0x43};
inline constexpr uint8_t kJD9365Data50[] = {0x4C};
inline constexpr uint8_t kJD9365Data51[] = {0x40};
inline constexpr uint8_t kJD9365Data52[] = {0x3D};
inline constexpr uint8_t kJD9365Data53[] = {0x31};
inline constexpr uint8_t kJD9365Data54[] = {0x20};
inline constexpr uint8_t kJD9365Data55[] = {0x0F};
inline constexpr uint8_t kJD9365Data56[] = {0x7F};
inline constexpr uint8_t kJD9365Data57[] = {0x56};
inline constexpr uint8_t kJD9365Data58[] = {0x43};
inline constexpr uint8_t kJD9365Data59[] = {0x37};
inline constexpr uint8_t kJD9365Data60[] = {0x33};
inline constexpr uint8_t kJD9365Data61[] = {0x25};
inline constexpr uint8_t kJD9365Data62[] = {0x2A};
inline constexpr uint8_t kJD9365Data63[] = {0x16};
inline constexpr uint8_t kJD9365Data64[] = {0x30};
inline constexpr uint8_t kJD9365Data65[] = {0x2F};
inline constexpr uint8_t kJD9365Data66[] = {0x32};
inline constexpr uint8_t kJD9365Data67[] = {0x53};
inline constexpr uint8_t kJD9365Data68[] = {0x43};
inline constexpr uint8_t kJD9365Data69[] = {0x4C};
inline constexpr uint8_t kJD9365Data70[] = {0x40};
inline constexpr uint8_t kJD9365Data71[] = {0x3D};
inline constexpr uint8_t kJD9365Data72[] = {0x31};
inline constexpr uint8_t kJD9365Data73[] = {0x20};
inline constexpr uint8_t kJD9365Data74[] = {0x0F};
inline constexpr uint8_t kJD9365Data75[] = {0x02};
inline constexpr uint8_t kJD9365Data76[] = {0x5F};
inline constexpr uint8_t kJD9365Data77[] = {0x5F};
inline constexpr uint8_t kJD9365Data78[] = {0x5E};
inline constexpr uint8_t kJD9365Data79[] = {0x5E};
inline constexpr uint8_t kJD9365Data80[] = {0x50};
inline constexpr uint8_t kJD9365Data81[] = {0x48};
inline constexpr uint8_t kJD9365Data82[] = {0x48};
inline constexpr uint8_t kJD9365Data83[] = {0x4A};
inline constexpr uint8_t kJD9365Data84[] = {0x4A};
inline constexpr uint8_t kJD9365Data85[] = {0x44};
inline constexpr uint8_t kJD9365Data86[] = {0x44};
inline constexpr uint8_t kJD9365Data87[] = {0x46};
inline constexpr uint8_t kJD9365Data88[] = {0x46};
inline constexpr uint8_t kJD9365Data89[] = {0x5F};
inline constexpr uint8_t kJD9365Data90[] = {0x5F};
inline constexpr uint8_t kJD9365Data91[] = {0x57};
inline constexpr uint8_t kJD9365Data92[] = {0x57};
inline constexpr uint8_t kJD9365Data93[] = {0x77};
inline constexpr uint8_t kJD9365Data94[] = {0x77};
inline constexpr uint8_t kJD9365Data95[] = {0x40};
inline constexpr uint8_t kJD9365Data96[] = {0x42};
inline constexpr uint8_t kJD9365Data97[] = {0x5F};
inline constexpr uint8_t kJD9365Data98[] = {0x5F};
inline constexpr uint8_t kJD9365Data99[] = {0x5F};
inline constexpr uint8_t kJD9365Data100[] = {0x5E};
inline constexpr uint8_t kJD9365Data101[] = {0x5E};
inline constexpr uint8_t kJD9365Data102[] = {0x50};
inline constexpr uint8_t kJD9365Data103[] = {0x49};
inline constexpr uint8_t kJD9365Data104[] = {0x49};
inline constexpr uint8_t kJD9365Data105[] = {0x4B};
inline constexpr uint8_t kJD9365Data106[] = {0x4B};
inline constexpr uint8_t kJD9365Data107[] = {0x45};
inline constexpr uint8_t kJD9365Data108[] = {0x45};
inline constexpr uint8_t kJD9365Data109[] = {0x47};
inline constexpr uint8_t kJD9365Data110[] = {0x47};
inline constexpr uint8_t kJD9365Data111[] = {0x5F};
inline constexpr uint8_t kJD9365Data112[] = {0x5F};
inline constexpr uint8_t kJD9365Data113[] = {0x57};
inline constexpr uint8_t kJD9365Data114[] = {0x57};
inline constexpr uint8_t kJD9365Data115[] = {0x77};
inline constexpr uint8_t kJD9365Data116[] = {0x77};
inline constexpr uint8_t kJD9365Data117[] = {0x41};
inline constexpr uint8_t kJD9365Data118[] = {0x43};
inline constexpr uint8_t kJD9365Data119[] = {0x5F};
inline constexpr uint8_t kJD9365Data120[] = {0x1E};
inline constexpr uint8_t kJD9365Data121[] = {0x1E};
inline constexpr uint8_t kJD9365Data122[] = {0x1F};
inline constexpr uint8_t kJD9365Data123[] = {0x1F};
inline constexpr uint8_t kJD9365Data124[] = {0x10};
inline constexpr uint8_t kJD9365Data125[] = {0x07};
inline constexpr uint8_t kJD9365Data126[] = {0x07};
inline constexpr uint8_t kJD9365Data127[] = {0x05};
inline constexpr uint8_t kJD9365Data128[] = {0x05};
inline constexpr uint8_t kJD9365Data129[] = {0x0B};
inline constexpr uint8_t kJD9365Data130[] = {0x0B};
inline constexpr uint8_t kJD9365Data131[] = {0x09};
inline constexpr uint8_t kJD9365Data132[] = {0x09};
inline constexpr uint8_t kJD9365Data133[] = {0x1F};
inline constexpr uint8_t kJD9365Data134[] = {0x1F};
inline constexpr uint8_t kJD9365Data135[] = {0x17};
inline constexpr uint8_t kJD9365Data136[] = {0x17};
inline constexpr uint8_t kJD9365Data137[] = {0x17};
inline constexpr uint8_t kJD9365Data138[] = {0x17};
inline constexpr uint8_t kJD9365Data139[] = {0x03};
inline constexpr uint8_t kJD9365Data140[] = {0x01};
inline constexpr uint8_t kJD9365Data141[] = {0x1F};
inline constexpr uint8_t kJD9365Data142[] = {0x1E};
inline constexpr uint8_t kJD9365Data143[] = {0x1E};
inline constexpr uint8_t kJD9365Data144[] = {0x1F};
inline constexpr uint8_t kJD9365Data145[] = {0x1F};
inline constexpr uint8_t kJD9365Data146[] = {0x10};
inline constexpr uint8_t kJD9365Data147[] = {0x06};
inline constexpr uint8_t kJD9365Data148[] = {0x06};
inline constexpr uint8_t kJD9365Data149[] = {0x04};
inline constexpr uint8_t kJD9365Data150[] = {0x04};
inline constexpr uint8_t kJD9365Data151[] = {0x0A};
inline constexpr uint8_t kJD9365Data152[] = {0x0A};
inline constexpr uint8_t kJD9365Data153[] = {0x08};
inline constexpr uint8_t kJD9365Data154[] = {0x08};
inline constexpr uint8_t kJD9365Data155[] = {0x1F};
inline constexpr uint8_t kJD9365Data156[] = {0x1F};
inline constexpr uint8_t kJD9365Data157[] = {0x17};
inline constexpr uint8_t kJD9365Data158[] = {0x17};
inline constexpr uint8_t kJD9365Data159[] = {0x17};
inline constexpr uint8_t kJD9365Data160[] = {0x17};
inline constexpr uint8_t kJD9365Data161[] = {0x02};
inline constexpr uint8_t kJD9365Data162[] = {0x00};
inline constexpr uint8_t kJD9365Data163[] = {0x1F};
inline constexpr uint8_t kJD9365Data164[] = {0x02};
inline constexpr uint8_t kJD9365Data165[] = {0x40};
inline constexpr uint8_t kJD9365Data166[] = {0x00};
inline constexpr uint8_t kJD9365Data167[] = {0x00};
inline constexpr uint8_t kJD9365Data168[] = {0x30};
inline constexpr uint8_t kJD9365Data169[] = {0x01};
inline constexpr uint8_t kJD9365Data170[] = {0x30};
inline constexpr uint8_t kJD9365Data171[] = {0x01};
inline constexpr uint8_t kJD9365Data172[] = {0x02};
inline constexpr uint8_t kJD9365Data173[] = {0x30};
inline constexpr uint8_t kJD9365Data174[] = {0x03};
inline constexpr uint8_t kJD9365Data175[] = {0x04};
inline constexpr uint8_t kJD9365Data176[] = {0x04};
inline constexpr uint8_t kJD9365Data177[] = {0xA6};
inline constexpr uint8_t kJD9365Data178[] = {0x43};
inline constexpr uint8_t kJD9365Data179[] = {0x30};
inline constexpr uint8_t kJD9365Data180[] = {0x73};
inline constexpr uint8_t kJD9365Data181[] = {0x05};
inline constexpr uint8_t kJD9365Data182[] = {0x04};
inline constexpr uint8_t kJD9365Data183[] = {0x7F};
inline constexpr uint8_t kJD9365Data184[] = {0x08};
inline constexpr uint8_t kJD9365Data185[] = {0x00};
inline constexpr uint8_t kJD9365Data186[] = {0x04};
inline constexpr uint8_t kJD9365Data187[] = {0x04};
inline constexpr uint8_t kJD9365Data188[] = {0x88};
inline constexpr uint8_t kJD9365Data189[] = {0xD9};
inline constexpr uint8_t kJD9365Data190[] = {0x00};
inline constexpr uint8_t kJD9365Data191[] = {0x33};
inline constexpr uint8_t kJD9365Data192[] = {0x43};
inline constexpr uint8_t kJD9365Data193[] = {0x00};
inline constexpr uint8_t kJD9365Data194[] = {0x00};
inline constexpr uint8_t kJD9365Data195[] = {0x00};
inline constexpr uint8_t kJD9365Data196[] = {0x00};
inline constexpr jd9365_lcd_init_cmd_t waveshare_p4_jd9365_init_cmds[] = {
    {0xE0, kJD9365Data0, 1, 0},
    {0xE1, kJD9365Data1, 1, 0},
    {0xE2, kJD9365Data2, 1, 0},
    {0xE3, kJD9365Data3, 1, 0},
    {0x80, kJD9365Data4, 1, 0},
    {0xE0, kJD9365Data5, 1, 0},
    {0x00, kJD9365Data6, 1, 0},
    {0x01, kJD9365Data7, 1, 0},
    {0x03, kJD9365Data8, 1, 0},
    {0x04, kJD9365Data9, 1, 0},
    {0x17, kJD9365Data10, 1, 0},
    {0x18, kJD9365Data11, 1, 0},
    {0x19, kJD9365Data12, 1, 0},
    {0x1A, kJD9365Data13, 1, 0},
    {0x1B, kJD9365Data14, 1, 0},
    {0x1C, kJD9365Data15, 1, 0},
    {0x24, kJD9365Data16, 1, 0},
    {0x35, kJD9365Data17, 1, 0},
    {0x37, kJD9365Data18, 1, 0},
    {0x38, kJD9365Data19, 1, 0},
    {0x39, kJD9365Data20, 1, 0},
    {0x3A, kJD9365Data21, 1, 0},
    {0x3C, kJD9365Data22, 1, 0},
    {0x3D, kJD9365Data23, 1, 0},
    {0x3E, kJD9365Data24, 1, 0},
    {0x3F, kJD9365Data25, 1, 0},
    {0x40, kJD9365Data26, 1, 0},
    {0x41, kJD9365Data27, 1, 0},
    {0x42, kJD9365Data28, 1, 0},
    {0x43, kJD9365Data29, 1, 0},
    {0x44, kJD9365Data30, 1, 0},
    {0x45, kJD9365Data31, 1, 0},
    {0x55, kJD9365Data32, 1, 0},
    {0x57, kJD9365Data33, 1, 0},
    {0x59, kJD9365Data34, 1, 0},
    {0x5A, kJD9365Data35, 1, 0},
    {0x5B, kJD9365Data36, 1, 0},
    {0x5D, kJD9365Data37, 1, 0},
    {0x5E, kJD9365Data38, 1, 0},
    {0x5F, kJD9365Data39, 1, 0},
    {0x60, kJD9365Data40, 1, 0},
    {0x61, kJD9365Data41, 1, 0},
    {0x62, kJD9365Data42, 1, 0},
    {0x63, kJD9365Data43, 1, 0},
    {0x64, kJD9365Data44, 1, 0},
    {0x65, kJD9365Data45, 1, 0},
    {0x66, kJD9365Data46, 1, 0},
    {0x67, kJD9365Data47, 1, 0},
    {0x68, kJD9365Data48, 1, 0},
    {0x69, kJD9365Data49, 1, 0},
    {0x6A, kJD9365Data50, 1, 0},
    {0x6B, kJD9365Data51, 1, 0},
    {0x6C, kJD9365Data52, 1, 0},
    {0x6D, kJD9365Data53, 1, 0},
    {0x6E, kJD9365Data54, 1, 0},
    {0x6F, kJD9365Data55, 1, 0},
    {0x70, kJD9365Data56, 1, 0},
    {0x71, kJD9365Data57, 1, 0},
    {0x72, kJD9365Data58, 1, 0},
    {0x73, kJD9365Data59, 1, 0},
    {0x74, kJD9365Data60, 1, 0},
    {0x75, kJD9365Data61, 1, 0},
    {0x76, kJD9365Data62, 1, 0},
    {0x77, kJD9365Data63, 1, 0},
    {0x78, kJD9365Data64, 1, 0},
    {0x79, kJD9365Data65, 1, 0},
    {0x7A, kJD9365Data66, 1, 0},
    {0x7B, kJD9365Data67, 1, 0},
    {0x7C, kJD9365Data68, 1, 0},
    {0x7D, kJD9365Data69, 1, 0},
    {0x7E, kJD9365Data70, 1, 0},
    {0x7F, kJD9365Data71, 1, 0},
    {0x80, kJD9365Data72, 1, 0},
    {0x81, kJD9365Data73, 1, 0},
    {0x82, kJD9365Data74, 1, 0},
    {0xE0, kJD9365Data75, 1, 0},
    {0x00, kJD9365Data76, 1, 0},
    {0x01, kJD9365Data77, 1, 0},
    {0x02, kJD9365Data78, 1, 0},
    {0x03, kJD9365Data79, 1, 0},
    {0x04, kJD9365Data80, 1, 0},
    {0x05, kJD9365Data81, 1, 0},
    {0x06, kJD9365Data82, 1, 0},
    {0x07, kJD9365Data83, 1, 0},
    {0x08, kJD9365Data84, 1, 0},
    {0x09, kJD9365Data85, 1, 0},
    {0x0A, kJD9365Data86, 1, 0},
    {0x0B, kJD9365Data87, 1, 0},
    {0x0C, kJD9365Data88, 1, 0},
    {0x0D, kJD9365Data89, 1, 0},
    {0x0E, kJD9365Data90, 1, 0},
    {0x0F, kJD9365Data91, 1, 0},
    {0x10, kJD9365Data92, 1, 0},
    {0x11, kJD9365Data93, 1, 0},
    {0x12, kJD9365Data94, 1, 0},
    {0x13, kJD9365Data95, 1, 0},
    {0x14, kJD9365Data96, 1, 0},
    {0x15, kJD9365Data97, 1, 0},
    {0x16, kJD9365Data98, 1, 0},
    {0x17, kJD9365Data99, 1, 0},
    {0x18, kJD9365Data100, 1, 0},
    {0x19, kJD9365Data101, 1, 0},
    {0x1A, kJD9365Data102, 1, 0},
    {0x1B, kJD9365Data103, 1, 0},
    {0x1C, kJD9365Data104, 1, 0},
    {0x1D, kJD9365Data105, 1, 0},
    {0x1E, kJD9365Data106, 1, 0},
    {0x1F, kJD9365Data107, 1, 0},
    {0x20, kJD9365Data108, 1, 0},
    {0x21, kJD9365Data109, 1, 0},
    {0x22, kJD9365Data110, 1, 0},
    {0x23, kJD9365Data111, 1, 0},
    {0x24, kJD9365Data112, 1, 0},
    {0x25, kJD9365Data113, 1, 0},
    {0x26, kJD9365Data114, 1, 0},
    {0x27, kJD9365Data115, 1, 0},
    {0x28, kJD9365Data116, 1, 0},
    {0x29, kJD9365Data117, 1, 0},
    {0x2A, kJD9365Data118, 1, 0},
    {0x2B, kJD9365Data119, 1, 0},
    {0x2C, kJD9365Data120, 1, 0},
    {0x2D, kJD9365Data121, 1, 0},
    {0x2E, kJD9365Data122, 1, 0},
    {0x2F, kJD9365Data123, 1, 0},
    {0x30, kJD9365Data124, 1, 0},
    {0x31, kJD9365Data125, 1, 0},
    {0x32, kJD9365Data126, 1, 0},
    {0x33, kJD9365Data127, 1, 0},
    {0x34, kJD9365Data128, 1, 0},
    {0x35, kJD9365Data129, 1, 0},
    {0x36, kJD9365Data130, 1, 0},
    {0x37, kJD9365Data131, 1, 0},
    {0x38, kJD9365Data132, 1, 0},
    {0x39, kJD9365Data133, 1, 0},
    {0x3A, kJD9365Data134, 1, 0},
    {0x3B, kJD9365Data135, 1, 0},
    {0x3C, kJD9365Data136, 1, 0},
    {0x3D, kJD9365Data137, 1, 0},
    {0x3E, kJD9365Data138, 1, 0},
    {0x3F, kJD9365Data139, 1, 0},
    {0x40, kJD9365Data140, 1, 0},
    {0x41, kJD9365Data141, 1, 0},
    {0x42, kJD9365Data142, 1, 0},
    {0x43, kJD9365Data143, 1, 0},
    {0x44, kJD9365Data144, 1, 0},
    {0x45, kJD9365Data145, 1, 0},
    {0x46, kJD9365Data146, 1, 0},
    {0x47, kJD9365Data147, 1, 0},
    {0x48, kJD9365Data148, 1, 0},
    {0x49, kJD9365Data149, 1, 0},
    {0x4A, kJD9365Data150, 1, 0},
    {0x4B, kJD9365Data151, 1, 0},
    {0x4C, kJD9365Data152, 1, 0},
    {0x4D, kJD9365Data153, 1, 0},
    {0x4E, kJD9365Data154, 1, 0},
    {0x4F, kJD9365Data155, 1, 0},
    {0x50, kJD9365Data156, 1, 0},
    {0x51, kJD9365Data157, 1, 0},
    {0x52, kJD9365Data158, 1, 0},
    {0x53, kJD9365Data159, 1, 0},
    {0x54, kJD9365Data160, 1, 0},
    {0x55, kJD9365Data161, 1, 0},
    {0x56, kJD9365Data162, 1, 0},
    {0x57, kJD9365Data163, 1, 0},
    {0xE0, kJD9365Data164, 1, 0},
    {0x58, kJD9365Data165, 1, 0},
    {0x59, kJD9365Data166, 1, 0},
    {0x5A, kJD9365Data167, 1, 0},
    {0x5B, kJD9365Data168, 1, 0},
    {0x5C, kJD9365Data169, 1, 0},
    {0x5D, kJD9365Data170, 1, 0},
    {0x5E, kJD9365Data171, 1, 0},
    {0x5F, kJD9365Data172, 1, 0},
    {0x60, kJD9365Data173, 1, 0},
    {0x61, kJD9365Data174, 1, 0},
    {0x62, kJD9365Data175, 1, 0},
    {0x63, kJD9365Data176, 1, 0},
    {0x64, kJD9365Data177, 1, 0},
    {0x65, kJD9365Data178, 1, 0},
    {0x66, kJD9365Data179, 1, 0},
    {0x67, kJD9365Data180, 1, 0},
    {0x68, kJD9365Data181, 1, 0},
    {0x69, kJD9365Data182, 1, 0},
    {0x6A, kJD9365Data183, 1, 0},
    {0x6B, kJD9365Data184, 1, 0},
    {0x6C, kJD9365Data185, 1, 0},
    {0x6D, kJD9365Data186, 1, 0},
    {0x6E, kJD9365Data187, 1, 0},
    {0x6F, kJD9365Data188, 1, 0},
    {0x75, kJD9365Data189, 1, 0},
    {0x76, kJD9365Data190, 1, 0},
    {0x77, kJD9365Data191, 1, 0},
    {0x78, kJD9365Data192, 1, 0},
    {0xE0, kJD9365Data193, 1, 0},
    {0x11, kJD9365Data194, 1, 120},
    {0x29, kJD9365Data195, 1, 20},
    {0x35, kJD9365Data196, 1, 0},
};

#elif defined(ASSISTANT_BOARD_P4_JD9365_4C)
inline constexpr uint8_t kJD9365Data0[] = {0x00};
inline constexpr uint8_t kJD9365Data1[] = {0x93};
inline constexpr uint8_t kJD9365Data2[] = {0x65};
inline constexpr uint8_t kJD9365Data3[] = {0xF8};
inline constexpr uint8_t kJD9365Data4[] = {0x01};
inline constexpr uint8_t kJD9365Data5[] = {0x01};
inline constexpr uint8_t kJD9365Data6[] = {0x00};
inline constexpr uint8_t kJD9365Data7[] = {0x41};
inline constexpr uint8_t kJD9365Data8[] = {0x10};
inline constexpr uint8_t kJD9365Data9[] = {0x44};
inline constexpr uint8_t kJD9365Data10[] = {0x00};
inline constexpr uint8_t kJD9365Data11[] = {0xD0};
inline constexpr uint8_t kJD9365Data12[] = {0x00};
inline constexpr uint8_t kJD9365Data13[] = {0x00};
inline constexpr uint8_t kJD9365Data14[] = {0xD0};
inline constexpr uint8_t kJD9365Data15[] = {0x00};
inline constexpr uint8_t kJD9365Data16[] = {0xFE};
inline constexpr uint8_t kJD9365Data17[] = {0x26};
inline constexpr uint8_t kJD9365Data18[] = {0x09};
inline constexpr uint8_t kJD9365Data19[] = {0x04};
inline constexpr uint8_t kJD9365Data20[] = {0x08};
inline constexpr uint8_t kJD9365Data21[] = {0x0A};
inline constexpr uint8_t kJD9365Data22[] = {0x78};
inline constexpr uint8_t kJD9365Data23[] = {0xFF};
inline constexpr uint8_t kJD9365Data24[] = {0xFF};
inline constexpr uint8_t kJD9365Data25[] = {0xFF};
inline constexpr uint8_t kJD9365Data26[] = {0x04};
inline constexpr uint8_t kJD9365Data27[] = {0x64};
inline constexpr uint8_t kJD9365Data28[] = {0xC7};
inline constexpr uint8_t kJD9365Data29[] = {0x18};
inline constexpr uint8_t kJD9365Data30[] = {0x0B};
inline constexpr uint8_t kJD9365Data31[] = {0x14};
inline constexpr uint8_t kJD9365Data32[] = {0x02};
inline constexpr uint8_t kJD9365Data33[] = {0x49};
inline constexpr uint8_t kJD9365Data34[] = {0x0A};
inline constexpr uint8_t kJD9365Data35[] = {0x1B};
inline constexpr uint8_t kJD9365Data36[] = {0x19};
inline constexpr uint8_t kJD9365Data37[] = {0x7F};
inline constexpr uint8_t kJD9365Data38[] = {0x56};
inline constexpr uint8_t kJD9365Data39[] = {0x43};
inline constexpr uint8_t kJD9365Data40[] = {0x37};
inline constexpr uint8_t kJD9365Data41[] = {0x33};
inline constexpr uint8_t kJD9365Data42[] = {0x25};
inline constexpr uint8_t kJD9365Data43[] = {0x2A};
inline constexpr uint8_t kJD9365Data44[] = {0x16};
inline constexpr uint8_t kJD9365Data45[] = {0x30};
inline constexpr uint8_t kJD9365Data46[] = {0x2F};
inline constexpr uint8_t kJD9365Data47[] = {0x32};
inline constexpr uint8_t kJD9365Data48[] = {0x53};
inline constexpr uint8_t kJD9365Data49[] = {0x43};
inline constexpr uint8_t kJD9365Data50[] = {0x4C};
inline constexpr uint8_t kJD9365Data51[] = {0x40};
inline constexpr uint8_t kJD9365Data52[] = {0x3D};
inline constexpr uint8_t kJD9365Data53[] = {0x31};
inline constexpr uint8_t kJD9365Data54[] = {0x20};
inline constexpr uint8_t kJD9365Data55[] = {0x0F};
inline constexpr uint8_t kJD9365Data56[] = {0x7F};
inline constexpr uint8_t kJD9365Data57[] = {0x56};
inline constexpr uint8_t kJD9365Data58[] = {0x43};
inline constexpr uint8_t kJD9365Data59[] = {0x37};
inline constexpr uint8_t kJD9365Data60[] = {0x33};
inline constexpr uint8_t kJD9365Data61[] = {0x25};
inline constexpr uint8_t kJD9365Data62[] = {0x2A};
inline constexpr uint8_t kJD9365Data63[] = {0x16};
inline constexpr uint8_t kJD9365Data64[] = {0x30};
inline constexpr uint8_t kJD9365Data65[] = {0x2F};
inline constexpr uint8_t kJD9365Data66[] = {0x32};
inline constexpr uint8_t kJD9365Data67[] = {0x53};
inline constexpr uint8_t kJD9365Data68[] = {0x43};
inline constexpr uint8_t kJD9365Data69[] = {0x4C};
inline constexpr uint8_t kJD9365Data70[] = {0x40};
inline constexpr uint8_t kJD9365Data71[] = {0x3D};
inline constexpr uint8_t kJD9365Data72[] = {0x31};
inline constexpr uint8_t kJD9365Data73[] = {0x20};
inline constexpr uint8_t kJD9365Data74[] = {0x0F};
inline constexpr uint8_t kJD9365Data75[] = {0x02};
inline constexpr uint8_t kJD9365Data76[] = {0x5F};
inline constexpr uint8_t kJD9365Data77[] = {0x5F};
inline constexpr uint8_t kJD9365Data78[] = {0x5E};
inline constexpr uint8_t kJD9365Data79[] = {0x5E};
inline constexpr uint8_t kJD9365Data80[] = {0x50};
inline constexpr uint8_t kJD9365Data81[] = {0x48};
inline constexpr uint8_t kJD9365Data82[] = {0x48};
inline constexpr uint8_t kJD9365Data83[] = {0x4A};
inline constexpr uint8_t kJD9365Data84[] = {0x4A};
inline constexpr uint8_t kJD9365Data85[] = {0x44};
inline constexpr uint8_t kJD9365Data86[] = {0x44};
inline constexpr uint8_t kJD9365Data87[] = {0x46};
inline constexpr uint8_t kJD9365Data88[] = {0x46};
inline constexpr uint8_t kJD9365Data89[] = {0x5F};
inline constexpr uint8_t kJD9365Data90[] = {0x5F};
inline constexpr uint8_t kJD9365Data91[] = {0x57};
inline constexpr uint8_t kJD9365Data92[] = {0x57};
inline constexpr uint8_t kJD9365Data93[] = {0x77};
inline constexpr uint8_t kJD9365Data94[] = {0x77};
inline constexpr uint8_t kJD9365Data95[] = {0x40};
inline constexpr uint8_t kJD9365Data96[] = {0x42};
inline constexpr uint8_t kJD9365Data97[] = {0x5F};
inline constexpr uint8_t kJD9365Data98[] = {0x5F};
inline constexpr uint8_t kJD9365Data99[] = {0x5F};
inline constexpr uint8_t kJD9365Data100[] = {0x5E};
inline constexpr uint8_t kJD9365Data101[] = {0x5E};
inline constexpr uint8_t kJD9365Data102[] = {0x50};
inline constexpr uint8_t kJD9365Data103[] = {0x49};
inline constexpr uint8_t kJD9365Data104[] = {0x49};
inline constexpr uint8_t kJD9365Data105[] = {0x4B};
inline constexpr uint8_t kJD9365Data106[] = {0x4B};
inline constexpr uint8_t kJD9365Data107[] = {0x45};
inline constexpr uint8_t kJD9365Data108[] = {0x45};
inline constexpr uint8_t kJD9365Data109[] = {0x47};
inline constexpr uint8_t kJD9365Data110[] = {0x47};
inline constexpr uint8_t kJD9365Data111[] = {0x5F};
inline constexpr uint8_t kJD9365Data112[] = {0x5F};
inline constexpr uint8_t kJD9365Data113[] = {0x57};
inline constexpr uint8_t kJD9365Data114[] = {0x57};
inline constexpr uint8_t kJD9365Data115[] = {0x77};
inline constexpr uint8_t kJD9365Data116[] = {0x77};
inline constexpr uint8_t kJD9365Data117[] = {0x41};
inline constexpr uint8_t kJD9365Data118[] = {0x43};
inline constexpr uint8_t kJD9365Data119[] = {0x5F};
inline constexpr uint8_t kJD9365Data120[] = {0x1E};
inline constexpr uint8_t kJD9365Data121[] = {0x1E};
inline constexpr uint8_t kJD9365Data122[] = {0x1F};
inline constexpr uint8_t kJD9365Data123[] = {0x1F};
inline constexpr uint8_t kJD9365Data124[] = {0x10};
inline constexpr uint8_t kJD9365Data125[] = {0x07};
inline constexpr uint8_t kJD9365Data126[] = {0x07};
inline constexpr uint8_t kJD9365Data127[] = {0x05};
inline constexpr uint8_t kJD9365Data128[] = {0x05};
inline constexpr uint8_t kJD9365Data129[] = {0x0B};
inline constexpr uint8_t kJD9365Data130[] = {0x0B};
inline constexpr uint8_t kJD9365Data131[] = {0x09};
inline constexpr uint8_t kJD9365Data132[] = {0x09};
inline constexpr uint8_t kJD9365Data133[] = {0x1F};
inline constexpr uint8_t kJD9365Data134[] = {0x1F};
inline constexpr uint8_t kJD9365Data135[] = {0x17};
inline constexpr uint8_t kJD9365Data136[] = {0x17};
inline constexpr uint8_t kJD9365Data137[] = {0x17};
inline constexpr uint8_t kJD9365Data138[] = {0x17};
inline constexpr uint8_t kJD9365Data139[] = {0x03};
inline constexpr uint8_t kJD9365Data140[] = {0x01};
inline constexpr uint8_t kJD9365Data141[] = {0x1F};
inline constexpr uint8_t kJD9365Data142[] = {0x1E};
inline constexpr uint8_t kJD9365Data143[] = {0x1E};
inline constexpr uint8_t kJD9365Data144[] = {0x1F};
inline constexpr uint8_t kJD9365Data145[] = {0x1F};
inline constexpr uint8_t kJD9365Data146[] = {0x10};
inline constexpr uint8_t kJD9365Data147[] = {0x06};
inline constexpr uint8_t kJD9365Data148[] = {0x06};
inline constexpr uint8_t kJD9365Data149[] = {0x04};
inline constexpr uint8_t kJD9365Data150[] = {0x04};
inline constexpr uint8_t kJD9365Data151[] = {0x0A};
inline constexpr uint8_t kJD9365Data152[] = {0x0A};
inline constexpr uint8_t kJD9365Data153[] = {0x08};
inline constexpr uint8_t kJD9365Data154[] = {0x08};
inline constexpr uint8_t kJD9365Data155[] = {0x1F};
inline constexpr uint8_t kJD9365Data156[] = {0x1F};
inline constexpr uint8_t kJD9365Data157[] = {0x17};
inline constexpr uint8_t kJD9365Data158[] = {0x17};
inline constexpr uint8_t kJD9365Data159[] = {0x17};
inline constexpr uint8_t kJD9365Data160[] = {0x17};
inline constexpr uint8_t kJD9365Data161[] = {0x02};
inline constexpr uint8_t kJD9365Data162[] = {0x00};
inline constexpr uint8_t kJD9365Data163[] = {0x1F};
inline constexpr uint8_t kJD9365Data164[] = {0x02};
inline constexpr uint8_t kJD9365Data165[] = {0x40};
inline constexpr uint8_t kJD9365Data166[] = {0x00};
inline constexpr uint8_t kJD9365Data167[] = {0x00};
inline constexpr uint8_t kJD9365Data168[] = {0x30};
inline constexpr uint8_t kJD9365Data169[] = {0x01};
inline constexpr uint8_t kJD9365Data170[] = {0x30};
inline constexpr uint8_t kJD9365Data171[] = {0x01};
inline constexpr uint8_t kJD9365Data172[] = {0x02};
inline constexpr uint8_t kJD9365Data173[] = {0x30};
inline constexpr uint8_t kJD9365Data174[] = {0x03};
inline constexpr uint8_t kJD9365Data175[] = {0x04};
inline constexpr uint8_t kJD9365Data176[] = {0x04};
inline constexpr uint8_t kJD9365Data177[] = {0xA6};
inline constexpr uint8_t kJD9365Data178[] = {0x43};
inline constexpr uint8_t kJD9365Data179[] = {0x30};
inline constexpr uint8_t kJD9365Data180[] = {0x73};
inline constexpr uint8_t kJD9365Data181[] = {0x05};
inline constexpr uint8_t kJD9365Data182[] = {0x04};
inline constexpr uint8_t kJD9365Data183[] = {0x7F};
inline constexpr uint8_t kJD9365Data184[] = {0x08};
inline constexpr uint8_t kJD9365Data185[] = {0x00};
inline constexpr uint8_t kJD9365Data186[] = {0x04};
inline constexpr uint8_t kJD9365Data187[] = {0x04};
inline constexpr uint8_t kJD9365Data188[] = {0x88};
inline constexpr uint8_t kJD9365Data189[] = {0xD9};
inline constexpr uint8_t kJD9365Data190[] = {0x00};
inline constexpr uint8_t kJD9365Data191[] = {0x33};
inline constexpr uint8_t kJD9365Data192[] = {0x43};
inline constexpr uint8_t kJD9365Data193[] = {0x00};
inline constexpr uint8_t kJD9365Data194[] = {0x00};
inline constexpr uint8_t kJD9365Data195[] = {0x00};
inline constexpr uint8_t kJD9365Data196[] = {0x00};
inline constexpr jd9365_lcd_init_cmd_t waveshare_p4_jd9365_init_cmds[] = {
    {0xE0, kJD9365Data0, 1, 0},
    {0xE1, kJD9365Data1, 1, 0},
    {0xE2, kJD9365Data2, 1, 0},
    {0xE3, kJD9365Data3, 1, 0},
    {0x80, kJD9365Data4, 1, 0},
    {0xE0, kJD9365Data5, 1, 0},
    {0x00, kJD9365Data6, 1, 0},
    {0x01, kJD9365Data7, 1, 0},
    {0x03, kJD9365Data8, 1, 0},
    {0x04, kJD9365Data9, 1, 0},
    {0x17, kJD9365Data10, 1, 0},
    {0x18, kJD9365Data11, 1, 0},
    {0x19, kJD9365Data12, 1, 0},
    {0x1A, kJD9365Data13, 1, 0},
    {0x1B, kJD9365Data14, 1, 0},
    {0x1C, kJD9365Data15, 1, 0},
    {0x24, kJD9365Data16, 1, 0},
    {0x35, kJD9365Data17, 1, 0},
    {0x37, kJD9365Data18, 1, 0},
    {0x38, kJD9365Data19, 1, 0},
    {0x39, kJD9365Data20, 1, 0},
    {0x3A, kJD9365Data21, 1, 0},
    {0x3C, kJD9365Data22, 1, 0},
    {0x3D, kJD9365Data23, 1, 0},
    {0x3E, kJD9365Data24, 1, 0},
    {0x3F, kJD9365Data25, 1, 0},
    {0x40, kJD9365Data26, 1, 0},
    {0x41, kJD9365Data27, 1, 0},
    {0x42, kJD9365Data28, 1, 0},
    {0x43, kJD9365Data29, 1, 0},
    {0x44, kJD9365Data30, 1, 0},
    {0x45, kJD9365Data31, 1, 0},
    {0x55, kJD9365Data32, 1, 0},
    {0x57, kJD9365Data33, 1, 0},
    {0x59, kJD9365Data34, 1, 0},
    {0x5A, kJD9365Data35, 1, 0},
    {0x5B, kJD9365Data36, 1, 0},
    {0x5D, kJD9365Data37, 1, 0},
    {0x5E, kJD9365Data38, 1, 0},
    {0x5F, kJD9365Data39, 1, 0},
    {0x60, kJD9365Data40, 1, 0},
    {0x61, kJD9365Data41, 1, 0},
    {0x62, kJD9365Data42, 1, 0},
    {0x63, kJD9365Data43, 1, 0},
    {0x64, kJD9365Data44, 1, 0},
    {0x65, kJD9365Data45, 1, 0},
    {0x66, kJD9365Data46, 1, 0},
    {0x67, kJD9365Data47, 1, 0},
    {0x68, kJD9365Data48, 1, 0},
    {0x69, kJD9365Data49, 1, 0},
    {0x6A, kJD9365Data50, 1, 0},
    {0x6B, kJD9365Data51, 1, 0},
    {0x6C, kJD9365Data52, 1, 0},
    {0x6D, kJD9365Data53, 1, 0},
    {0x6E, kJD9365Data54, 1, 0},
    {0x6F, kJD9365Data55, 1, 0},
    {0x70, kJD9365Data56, 1, 0},
    {0x71, kJD9365Data57, 1, 0},
    {0x72, kJD9365Data58, 1, 0},
    {0x73, kJD9365Data59, 1, 0},
    {0x74, kJD9365Data60, 1, 0},
    {0x75, kJD9365Data61, 1, 0},
    {0x76, kJD9365Data62, 1, 0},
    {0x77, kJD9365Data63, 1, 0},
    {0x78, kJD9365Data64, 1, 0},
    {0x79, kJD9365Data65, 1, 0},
    {0x7A, kJD9365Data66, 1, 0},
    {0x7B, kJD9365Data67, 1, 0},
    {0x7C, kJD9365Data68, 1, 0},
    {0x7D, kJD9365Data69, 1, 0},
    {0x7E, kJD9365Data70, 1, 0},
    {0x7F, kJD9365Data71, 1, 0},
    {0x80, kJD9365Data72, 1, 0},
    {0x81, kJD9365Data73, 1, 0},
    {0x82, kJD9365Data74, 1, 0},
    {0xE0, kJD9365Data75, 1, 0},
    {0x00, kJD9365Data76, 1, 0},
    {0x01, kJD9365Data77, 1, 0},
    {0x02, kJD9365Data78, 1, 0},
    {0x03, kJD9365Data79, 1, 0},
    {0x04, kJD9365Data80, 1, 0},
    {0x05, kJD9365Data81, 1, 0},
    {0x06, kJD9365Data82, 1, 0},
    {0x07, kJD9365Data83, 1, 0},
    {0x08, kJD9365Data84, 1, 0},
    {0x09, kJD9365Data85, 1, 0},
    {0x0A, kJD9365Data86, 1, 0},
    {0x0B, kJD9365Data87, 1, 0},
    {0x0C, kJD9365Data88, 1, 0},
    {0x0D, kJD9365Data89, 1, 0},
    {0x0E, kJD9365Data90, 1, 0},
    {0x0F, kJD9365Data91, 1, 0},
    {0x10, kJD9365Data92, 1, 0},
    {0x11, kJD9365Data93, 1, 0},
    {0x12, kJD9365Data94, 1, 0},
    {0x13, kJD9365Data95, 1, 0},
    {0x14, kJD9365Data96, 1, 0},
    {0x15, kJD9365Data97, 1, 0},
    {0x16, kJD9365Data98, 1, 0},
    {0x17, kJD9365Data99, 1, 0},
    {0x18, kJD9365Data100, 1, 0},
    {0x19, kJD9365Data101, 1, 0},
    {0x1A, kJD9365Data102, 1, 0},
    {0x1B, kJD9365Data103, 1, 0},
    {0x1C, kJD9365Data104, 1, 0},
    {0x1D, kJD9365Data105, 1, 0},
    {0x1E, kJD9365Data106, 1, 0},
    {0x1F, kJD9365Data107, 1, 0},
    {0x20, kJD9365Data108, 1, 0},
    {0x21, kJD9365Data109, 1, 0},
    {0x22, kJD9365Data110, 1, 0},
    {0x23, kJD9365Data111, 1, 0},
    {0x24, kJD9365Data112, 1, 0},
    {0x25, kJD9365Data113, 1, 0},
    {0x26, kJD9365Data114, 1, 0},
    {0x27, kJD9365Data115, 1, 0},
    {0x28, kJD9365Data116, 1, 0},
    {0x29, kJD9365Data117, 1, 0},
    {0x2A, kJD9365Data118, 1, 0},
    {0x2B, kJD9365Data119, 1, 0},
    {0x2C, kJD9365Data120, 1, 0},
    {0x2D, kJD9365Data121, 1, 0},
    {0x2E, kJD9365Data122, 1, 0},
    {0x2F, kJD9365Data123, 1, 0},
    {0x30, kJD9365Data124, 1, 0},
    {0x31, kJD9365Data125, 1, 0},
    {0x32, kJD9365Data126, 1, 0},
    {0x33, kJD9365Data127, 1, 0},
    {0x34, kJD9365Data128, 1, 0},
    {0x35, kJD9365Data129, 1, 0},
    {0x36, kJD9365Data130, 1, 0},
    {0x37, kJD9365Data131, 1, 0},
    {0x38, kJD9365Data132, 1, 0},
    {0x39, kJD9365Data133, 1, 0},
    {0x3A, kJD9365Data134, 1, 0},
    {0x3B, kJD9365Data135, 1, 0},
    {0x3C, kJD9365Data136, 1, 0},
    {0x3D, kJD9365Data137, 1, 0},
    {0x3E, kJD9365Data138, 1, 0},
    {0x3F, kJD9365Data139, 1, 0},
    {0x40, kJD9365Data140, 1, 0},
    {0x41, kJD9365Data141, 1, 0},
    {0x42, kJD9365Data142, 1, 0},
    {0x43, kJD9365Data143, 1, 0},
    {0x44, kJD9365Data144, 1, 0},
    {0x45, kJD9365Data145, 1, 0},
    {0x46, kJD9365Data146, 1, 0},
    {0x47, kJD9365Data147, 1, 0},
    {0x48, kJD9365Data148, 1, 0},
    {0x49, kJD9365Data149, 1, 0},
    {0x4A, kJD9365Data150, 1, 0},
    {0x4B, kJD9365Data151, 1, 0},
    {0x4C, kJD9365Data152, 1, 0},
    {0x4D, kJD9365Data153, 1, 0},
    {0x4E, kJD9365Data154, 1, 0},
    {0x4F, kJD9365Data155, 1, 0},
    {0x50, kJD9365Data156, 1, 0},
    {0x51, kJD9365Data157, 1, 0},
    {0x52, kJD9365Data158, 1, 0},
    {0x53, kJD9365Data159, 1, 0},
    {0x54, kJD9365Data160, 1, 0},
    {0x55, kJD9365Data161, 1, 0},
    {0x56, kJD9365Data162, 1, 0},
    {0x57, kJD9365Data163, 1, 0},
    {0xE0, kJD9365Data164, 1, 0},
    {0x58, kJD9365Data165, 1, 0},
    {0x59, kJD9365Data166, 1, 0},
    {0x5A, kJD9365Data167, 1, 0},
    {0x5B, kJD9365Data168, 1, 0},
    {0x5C, kJD9365Data169, 1, 0},
    {0x5D, kJD9365Data170, 1, 0},
    {0x5E, kJD9365Data171, 1, 0},
    {0x5F, kJD9365Data172, 1, 0},
    {0x60, kJD9365Data173, 1, 0},
    {0x61, kJD9365Data174, 1, 0},
    {0x62, kJD9365Data175, 1, 0},
    {0x63, kJD9365Data176, 1, 0},
    {0x64, kJD9365Data177, 1, 0},
    {0x65, kJD9365Data178, 1, 0},
    {0x66, kJD9365Data179, 1, 0},
    {0x67, kJD9365Data180, 1, 0},
    {0x68, kJD9365Data181, 1, 0},
    {0x69, kJD9365Data182, 1, 0},
    {0x6A, kJD9365Data183, 1, 0},
    {0x6B, kJD9365Data184, 1, 0},
    {0x6C, kJD9365Data185, 1, 0},
    {0x6D, kJD9365Data186, 1, 0},
    {0x6E, kJD9365Data187, 1, 0},
    {0x6F, kJD9365Data188, 1, 0},
    {0x75, kJD9365Data189, 1, 0},
    {0x76, kJD9365Data190, 1, 0},
    {0x77, kJD9365Data191, 1, 0},
    {0x78, kJD9365Data192, 1, 0},
    {0xE0, kJD9365Data193, 1, 0},
    {0x11, kJD9365Data194, 1, 120},
    {0x29, kJD9365Data195, 1, 20},
    {0x35, kJD9365Data196, 1, 0},
};

#elif defined(ASSISTANT_BOARD_P4_JD9365_8)
inline constexpr uint8_t kJD9365Data0[] = {0x00};
inline constexpr uint8_t kJD9365Data1[] = {0x93};
inline constexpr uint8_t kJD9365Data2[] = {0x65};
inline constexpr uint8_t kJD9365Data3[] = {0xF8};
inline constexpr uint8_t kJD9365Data4[] = {0x01};
inline constexpr uint8_t kJD9365Data5[] = {0x01};
inline constexpr uint8_t kJD9365Data6[] = {0x00};
inline constexpr uint8_t kJD9365Data7[] = {0x4E};
inline constexpr uint8_t kJD9365Data8[] = {0x00};
inline constexpr uint8_t kJD9365Data9[] = {0x65};
inline constexpr uint8_t kJD9365Data10[] = {0x74};
inline constexpr uint8_t kJD9365Data11[] = {0x00};
inline constexpr uint8_t kJD9365Data12[] = {0xB7};
inline constexpr uint8_t kJD9365Data13[] = {0x00};
inline constexpr uint8_t kJD9365Data14[] = {0x00};
inline constexpr uint8_t kJD9365Data15[] = {0xB7};
inline constexpr uint8_t kJD9365Data16[] = {0x00};
inline constexpr uint8_t kJD9365Data17[] = {0xFE};
inline constexpr uint8_t kJD9365Data18[] = {0x19};
inline constexpr uint8_t kJD9365Data19[] = {0x05};
inline constexpr uint8_t kJD9365Data20[] = {0x00};
inline constexpr uint8_t kJD9365Data21[] = {0x01};
inline constexpr uint8_t kJD9365Data22[] = {0x01};
inline constexpr uint8_t kJD9365Data23[] = {0x70};
inline constexpr uint8_t kJD9365Data24[] = {0xFF};
inline constexpr uint8_t kJD9365Data25[] = {0xFF};
inline constexpr uint8_t kJD9365Data26[] = {0xFF};
inline constexpr uint8_t kJD9365Data27[] = {0x06};
inline constexpr uint8_t kJD9365Data28[] = {0xA0};
inline constexpr uint8_t kJD9365Data29[] = {0x1E};
inline constexpr uint8_t kJD9365Data30[] = {0x0F};
inline constexpr uint8_t kJD9365Data31[] = {0x28};
inline constexpr uint8_t kJD9365Data32[] = {0x04};
inline constexpr uint8_t kJD9365Data33[] = {0x02};
inline constexpr uint8_t kJD9365Data34[] = {0x01};
inline constexpr uint8_t kJD9365Data35[] = {0xA9};
inline constexpr uint8_t kJD9365Data36[] = {0x0A};
inline constexpr uint8_t kJD9365Data37[] = {0x0A};
inline constexpr uint8_t kJD9365Data38[] = {0x37};
inline constexpr uint8_t kJD9365Data39[] = {0x19};
inline constexpr uint8_t kJD9365Data40[] = {0x78};
inline constexpr uint8_t kJD9365Data41[] = {0x63};
inline constexpr uint8_t kJD9365Data42[] = {0x54};
inline constexpr uint8_t kJD9365Data43[] = {0x49};
inline constexpr uint8_t kJD9365Data44[] = {0x45};
inline constexpr uint8_t kJD9365Data45[] = {0x38};
inline constexpr uint8_t kJD9365Data46[] = {0x3D};
inline constexpr uint8_t kJD9365Data47[] = {0x28};
inline constexpr uint8_t kJD9365Data48[] = {0x43};
inline constexpr uint8_t kJD9365Data49[] = {0x41};
inline constexpr uint8_t kJD9365Data50[] = {0x43};
inline constexpr uint8_t kJD9365Data51[] = {0x62};
inline constexpr uint8_t kJD9365Data52[] = {0x50};
inline constexpr uint8_t kJD9365Data53[] = {0x57};
inline constexpr uint8_t kJD9365Data54[] = {0x49};
inline constexpr uint8_t kJD9365Data55[] = {0x44};
inline constexpr uint8_t kJD9365Data56[] = {0x37};
inline constexpr uint8_t kJD9365Data57[] = {0x23};
inline constexpr uint8_t kJD9365Data58[] = {0x10};
inline constexpr uint8_t kJD9365Data59[] = {0x78};
inline constexpr uint8_t kJD9365Data60[] = {0x63};
inline constexpr uint8_t kJD9365Data61[] = {0x54};
inline constexpr uint8_t kJD9365Data62[] = {0x49};
inline constexpr uint8_t kJD9365Data63[] = {0x45};
inline constexpr uint8_t kJD9365Data64[] = {0x38};
inline constexpr uint8_t kJD9365Data65[] = {0x3D};
inline constexpr uint8_t kJD9365Data66[] = {0x28};
inline constexpr uint8_t kJD9365Data67[] = {0x43};
inline constexpr uint8_t kJD9365Data68[] = {0x41};
inline constexpr uint8_t kJD9365Data69[] = {0x43};
inline constexpr uint8_t kJD9365Data70[] = {0x62};
inline constexpr uint8_t kJD9365Data71[] = {0x50};
inline constexpr uint8_t kJD9365Data72[] = {0x57};
inline constexpr uint8_t kJD9365Data73[] = {0x49};
inline constexpr uint8_t kJD9365Data74[] = {0x44};
inline constexpr uint8_t kJD9365Data75[] = {0x37};
inline constexpr uint8_t kJD9365Data76[] = {0x23};
inline constexpr uint8_t kJD9365Data77[] = {0x10};
inline constexpr uint8_t kJD9365Data78[] = {0x02};
inline constexpr uint8_t kJD9365Data79[] = {0x47};
inline constexpr uint8_t kJD9365Data80[] = {0x47};
inline constexpr uint8_t kJD9365Data81[] = {0x45};
inline constexpr uint8_t kJD9365Data82[] = {0x45};
inline constexpr uint8_t kJD9365Data83[] = {0x4B};
inline constexpr uint8_t kJD9365Data84[] = {0x4B};
inline constexpr uint8_t kJD9365Data85[] = {0x49};
inline constexpr uint8_t kJD9365Data86[] = {0x49};
inline constexpr uint8_t kJD9365Data87[] = {0x41};
inline constexpr uint8_t kJD9365Data88[] = {0x1F};
inline constexpr uint8_t kJD9365Data89[] = {0x1F};
inline constexpr uint8_t kJD9365Data90[] = {0x1F};
inline constexpr uint8_t kJD9365Data91[] = {0x1F};
inline constexpr uint8_t kJD9365Data92[] = {0x1F};
inline constexpr uint8_t kJD9365Data93[] = {0x1F};
inline constexpr uint8_t kJD9365Data94[] = {0x5F};
inline constexpr uint8_t kJD9365Data95[] = {0x5F};
inline constexpr uint8_t kJD9365Data96[] = {0x57};
inline constexpr uint8_t kJD9365Data97[] = {0x77};
inline constexpr uint8_t kJD9365Data98[] = {0x35};
inline constexpr uint8_t kJD9365Data99[] = {0x1F};
inline constexpr uint8_t kJD9365Data100[] = {0x1F};
inline constexpr uint8_t kJD9365Data101[] = {0x46};
inline constexpr uint8_t kJD9365Data102[] = {0x46};
inline constexpr uint8_t kJD9365Data103[] = {0x44};
inline constexpr uint8_t kJD9365Data104[] = {0x44};
inline constexpr uint8_t kJD9365Data105[] = {0x4A};
inline constexpr uint8_t kJD9365Data106[] = {0x4A};
inline constexpr uint8_t kJD9365Data107[] = {0x48};
inline constexpr uint8_t kJD9365Data108[] = {0x48};
inline constexpr uint8_t kJD9365Data109[] = {0x40};
inline constexpr uint8_t kJD9365Data110[] = {0x1F};
inline constexpr uint8_t kJD9365Data111[] = {0x1F};
inline constexpr uint8_t kJD9365Data112[] = {0x1F};
inline constexpr uint8_t kJD9365Data113[] = {0x1F};
inline constexpr uint8_t kJD9365Data114[] = {0x1F};
inline constexpr uint8_t kJD9365Data115[] = {0x1F};
inline constexpr uint8_t kJD9365Data116[] = {0x5F};
inline constexpr uint8_t kJD9365Data117[] = {0x5F};
inline constexpr uint8_t kJD9365Data118[] = {0x57};
inline constexpr uint8_t kJD9365Data119[] = {0x77};
inline constexpr uint8_t kJD9365Data120[] = {0x35};
inline constexpr uint8_t kJD9365Data121[] = {0x1F};
inline constexpr uint8_t kJD9365Data122[] = {0x1F};
inline constexpr uint8_t kJD9365Data123[] = {0x40};
inline constexpr uint8_t kJD9365Data124[] = {0x00};
inline constexpr uint8_t kJD9365Data125[] = {0x00};
inline constexpr uint8_t kJD9365Data126[] = {0x10};
inline constexpr uint8_t kJD9365Data127[] = {0x06};
inline constexpr uint8_t kJD9365Data128[] = {0x40};
inline constexpr uint8_t kJD9365Data129[] = {0x01};
inline constexpr uint8_t kJD9365Data130[] = {0x02};
inline constexpr uint8_t kJD9365Data131[] = {0x30};
inline constexpr uint8_t kJD9365Data132[] = {0x01};
inline constexpr uint8_t kJD9365Data133[] = {0x02};
inline constexpr uint8_t kJD9365Data134[] = {0x03};
inline constexpr uint8_t kJD9365Data135[] = {0x6B};
inline constexpr uint8_t kJD9365Data136[] = {0x05};
inline constexpr uint8_t kJD9365Data137[] = {0x0C};
inline constexpr uint8_t kJD9365Data138[] = {0x73};
inline constexpr uint8_t kJD9365Data139[] = {0x09};
inline constexpr uint8_t kJD9365Data140[] = {0x03};
inline constexpr uint8_t kJD9365Data141[] = {0x56};
inline constexpr uint8_t kJD9365Data142[] = {0x08};
inline constexpr uint8_t kJD9365Data143[] = {0x00};
inline constexpr uint8_t kJD9365Data144[] = {0x04};
inline constexpr uint8_t kJD9365Data145[] = {0x04};
inline constexpr uint8_t kJD9365Data146[] = {0x88};
inline constexpr uint8_t kJD9365Data147[] = {0x00};
inline constexpr uint8_t kJD9365Data148[] = {0x00};
inline constexpr uint8_t kJD9365Data149[] = {0x06};
inline constexpr uint8_t kJD9365Data150[] = {0x7B};
inline constexpr uint8_t kJD9365Data151[] = {0x00};
inline constexpr uint8_t kJD9365Data152[] = {0xF8};
inline constexpr uint8_t kJD9365Data153[] = {0x00};
inline constexpr uint8_t kJD9365Data154[] = {0xD5};
inline constexpr uint8_t kJD9365Data155[] = {0x2E};
inline constexpr uint8_t kJD9365Data156[] = {0x12};
inline constexpr uint8_t kJD9365Data157[] = {0x03};
inline constexpr uint8_t kJD9365Data158[] = {0x00};
inline constexpr uint8_t kJD9365Data159[] = {0x00};
inline constexpr uint8_t kJD9365Data160[] = {0x03};
inline constexpr uint8_t kJD9365Data161[] = {0x7B};
inline constexpr uint8_t kJD9365Data162[] = {0x04};
inline constexpr uint8_t kJD9365Data163[] = {0x0E};
inline constexpr uint8_t kJD9365Data164[] = {0xB3};
inline constexpr uint8_t kJD9365Data165[] = {0x60};
inline constexpr uint8_t kJD9365Data166[] = {0x2A};
inline constexpr uint8_t kJD9365Data167[] = {0x59};
inline constexpr uint8_t kJD9365Data168[] = {0x58};
inline constexpr uint8_t kJD9365Data169[] = {0x0F};
inline constexpr uint8_t kJD9365Data170[] = {0x00};
inline constexpr uint8_t kJD9365Data171[] = {0x00};
inline constexpr uint8_t kJD9365Data172[] = {0x00};
inline constexpr uint8_t kJD9365Data173[] = {0x00};
inline constexpr jd9365_lcd_init_cmd_t waveshare_p4_jd9365_init_cmds[] = {
    {0xE0, kJD9365Data0, 1, 0},
    {0xE1, kJD9365Data1, 1, 0},
    {0xE2, kJD9365Data2, 1, 0},
    {0xE3, kJD9365Data3, 1, 0},
    {0x80, kJD9365Data4, 1, 0},
    {0xE0, kJD9365Data5, 1, 0},
    {0x00, kJD9365Data6, 1, 0},
    {0x01, kJD9365Data7, 1, 0},
    {0x03, kJD9365Data8, 1, 0},
    {0x04, kJD9365Data9, 1, 0},
    {0x0C, kJD9365Data10, 1, 0},
    {0x17, kJD9365Data11, 1, 0},
    {0x18, kJD9365Data12, 1, 0},
    {0x19, kJD9365Data13, 1, 0},
    {0x1A, kJD9365Data14, 1, 0},
    {0x1B, kJD9365Data15, 1, 0},
    {0x1C, kJD9365Data16, 1, 0},
    {0x24, kJD9365Data17, 1, 0},
    {0x37, kJD9365Data18, 1, 0},
    {0x38, kJD9365Data19, 1, 0},
    {0x39, kJD9365Data20, 1, 0},
    {0x3A, kJD9365Data21, 1, 0},
    {0x3B, kJD9365Data22, 1, 0},
    {0x3C, kJD9365Data23, 1, 0},
    {0x3D, kJD9365Data24, 1, 0},
    {0x3E, kJD9365Data25, 1, 0},
    {0x3F, kJD9365Data26, 1, 0},
    {0x40, kJD9365Data27, 1, 0},
    {0x41, kJD9365Data28, 1, 0},
    {0x43, kJD9365Data29, 1, 0},
    {0x44, kJD9365Data30, 1, 0},
    {0x45, kJD9365Data31, 1, 0},
    {0x4B, kJD9365Data32, 1, 0},
    {0x55, kJD9365Data33, 1, 0},
    {0x56, kJD9365Data34, 1, 0},
    {0x57, kJD9365Data35, 1, 0},
    {0x58, kJD9365Data36, 1, 0},
    {0x59, kJD9365Data37, 1, 0},
    {0x5A, kJD9365Data38, 1, 0},
    {0x5B, kJD9365Data39, 1, 0},
    {0x5D, kJD9365Data40, 1, 0},
    {0x5E, kJD9365Data41, 1, 0},
    {0x5F, kJD9365Data42, 1, 0},
    {0x60, kJD9365Data43, 1, 0},
    {0x61, kJD9365Data44, 1, 0},
    {0x62, kJD9365Data45, 1, 0},
    {0x63, kJD9365Data46, 1, 0},
    {0x64, kJD9365Data47, 1, 0},
    {0x65, kJD9365Data48, 1, 0},
    {0x66, kJD9365Data49, 1, 0},
    {0x67, kJD9365Data50, 1, 0},
    {0x68, kJD9365Data51, 1, 0},
    {0x69, kJD9365Data52, 1, 0},
    {0x6A, kJD9365Data53, 1, 0},
    {0x6B, kJD9365Data54, 1, 0},
    {0x6C, kJD9365Data55, 1, 0},
    {0x6D, kJD9365Data56, 1, 0},
    {0x6E, kJD9365Data57, 1, 0},
    {0x6F, kJD9365Data58, 1, 0},
    {0x70, kJD9365Data59, 1, 0},
    {0x71, kJD9365Data60, 1, 0},
    {0x72, kJD9365Data61, 1, 0},
    {0x73, kJD9365Data62, 1, 0},
    {0x74, kJD9365Data63, 1, 0},
    {0x75, kJD9365Data64, 1, 0},
    {0x76, kJD9365Data65, 1, 0},
    {0x77, kJD9365Data66, 1, 0},
    {0x78, kJD9365Data67, 1, 0},
    {0x79, kJD9365Data68, 1, 0},
    {0x7A, kJD9365Data69, 1, 0},
    {0x7B, kJD9365Data70, 1, 0},
    {0x7C, kJD9365Data71, 1, 0},
    {0x7D, kJD9365Data72, 1, 0},
    {0x7E, kJD9365Data73, 1, 0},
    {0x7F, kJD9365Data74, 1, 0},
    {0x80, kJD9365Data75, 1, 0},
    {0x81, kJD9365Data76, 1, 0},
    {0x82, kJD9365Data77, 1, 0},
    {0xE0, kJD9365Data78, 1, 0},
    {0x00, kJD9365Data79, 1, 0},
    {0x01, kJD9365Data80, 1, 0},
    {0x02, kJD9365Data81, 1, 0},
    {0x03, kJD9365Data82, 1, 0},
    {0x04, kJD9365Data83, 1, 0},
    {0x05, kJD9365Data84, 1, 0},
    {0x06, kJD9365Data85, 1, 0},
    {0x07, kJD9365Data86, 1, 0},
    {0x08, kJD9365Data87, 1, 0},
    {0x09, kJD9365Data88, 1, 0},
    {0x0A, kJD9365Data89, 1, 0},
    {0x0B, kJD9365Data90, 1, 0},
    {0x0C, kJD9365Data91, 1, 0},
    {0x0D, kJD9365Data92, 1, 0},
    {0x0E, kJD9365Data93, 1, 0},
    {0x0F, kJD9365Data94, 1, 0},
    {0x10, kJD9365Data95, 1, 0},
    {0x11, kJD9365Data96, 1, 0},
    {0x12, kJD9365Data97, 1, 0},
    {0x13, kJD9365Data98, 1, 0},
    {0x14, kJD9365Data99, 1, 0},
    {0x15, kJD9365Data100, 1, 0},
    {0x16, kJD9365Data101, 1, 0},
    {0x17, kJD9365Data102, 1, 0},
    {0x18, kJD9365Data103, 1, 0},
    {0x19, kJD9365Data104, 1, 0},
    {0x1A, kJD9365Data105, 1, 0},
    {0x1B, kJD9365Data106, 1, 0},
    {0x1C, kJD9365Data107, 1, 0},
    {0x1D, kJD9365Data108, 1, 0},
    {0x1E, kJD9365Data109, 1, 0},
    {0x1F, kJD9365Data110, 1, 0},
    {0x20, kJD9365Data111, 1, 0},
    {0x21, kJD9365Data112, 1, 0},
    {0x22, kJD9365Data113, 1, 0},
    {0x23, kJD9365Data114, 1, 0},
    {0x24, kJD9365Data115, 1, 0},
    {0x25, kJD9365Data116, 1, 0},
    {0x26, kJD9365Data117, 1, 0},
    {0x27, kJD9365Data118, 1, 0},
    {0x28, kJD9365Data119, 1, 0},
    {0x29, kJD9365Data120, 1, 0},
    {0x2A, kJD9365Data121, 1, 0},
    {0x2B, kJD9365Data122, 1, 0},
    {0x58, kJD9365Data123, 1, 0},
    {0x59, kJD9365Data124, 1, 0},
    {0x5A, kJD9365Data125, 1, 0},
    {0x5B, kJD9365Data126, 1, 0},
    {0x5C, kJD9365Data127, 1, 0},
    {0x5D, kJD9365Data128, 1, 0},
    {0x5E, kJD9365Data129, 1, 0},
    {0x5F, kJD9365Data130, 1, 0},
    {0x60, kJD9365Data131, 1, 0},
    {0x61, kJD9365Data132, 1, 0},
    {0x62, kJD9365Data133, 1, 0},
    {0x63, kJD9365Data134, 1, 0},
    {0x64, kJD9365Data135, 1, 0},
    {0x65, kJD9365Data136, 1, 0},
    {0x66, kJD9365Data137, 1, 0},
    {0x67, kJD9365Data138, 1, 0},
    {0x68, kJD9365Data139, 1, 0},
    {0x69, kJD9365Data140, 1, 0},
    {0x6A, kJD9365Data141, 1, 0},
    {0x6B, kJD9365Data142, 1, 0},
    {0x6C, kJD9365Data143, 1, 0},
    {0x6D, kJD9365Data144, 1, 0},
    {0x6E, kJD9365Data145, 1, 0},
    {0x6F, kJD9365Data146, 1, 0},
    {0x70, kJD9365Data147, 1, 0},
    {0x71, kJD9365Data148, 1, 0},
    {0x72, kJD9365Data149, 1, 0},
    {0x73, kJD9365Data150, 1, 0},
    {0x74, kJD9365Data151, 1, 0},
    {0x75, kJD9365Data152, 1, 0},
    {0x76, kJD9365Data153, 1, 0},
    {0x77, kJD9365Data154, 1, 0},
    {0x78, kJD9365Data155, 1, 0},
    {0x79, kJD9365Data156, 1, 0},
    {0x7A, kJD9365Data157, 1, 0},
    {0x7B, kJD9365Data158, 1, 0},
    {0x7C, kJD9365Data159, 1, 0},
    {0x7D, kJD9365Data160, 1, 0},
    {0x7E, kJD9365Data161, 1, 0},
    {0xE0, kJD9365Data162, 1, 0},
    {0x00, kJD9365Data163, 1, 0},
    {0x02, kJD9365Data164, 1, 0},
    {0x09, kJD9365Data165, 1, 0},
    {0x0E, kJD9365Data166, 1, 0},
    {0x36, kJD9365Data167, 1, 0},
    {0x37, kJD9365Data168, 1, 0},
    {0x2B, kJD9365Data169, 1, 0},
    {0xE0, kJD9365Data170, 1, 0},
    {0x11, kJD9365Data171, 1, 120},
    {0x29, kJD9365Data172, 1, 20},
    {0x35, kJD9365Data173, 1, 0},
};

#elif defined(ASSISTANT_BOARD_P4_JD9365_101)
inline constexpr uint8_t kJD9365Data0[] = {0x00};
inline constexpr uint8_t kJD9365Data1[] = {0x93};
inline constexpr uint8_t kJD9365Data2[] = {0x65};
inline constexpr uint8_t kJD9365Data3[] = {0xF8};
inline constexpr uint8_t kJD9365Data4[] = {0x01};
inline constexpr uint8_t kJD9365Data5[] = {0x01};
inline constexpr uint8_t kJD9365Data6[] = {0x00};
inline constexpr uint8_t kJD9365Data7[] = {0x38};
inline constexpr uint8_t kJD9365Data8[] = {0x10};
inline constexpr uint8_t kJD9365Data9[] = {0x38};
inline constexpr uint8_t kJD9365Data10[] = {0x74};
inline constexpr uint8_t kJD9365Data11[] = {0x00};
inline constexpr uint8_t kJD9365Data12[] = {0xAF};
inline constexpr uint8_t kJD9365Data13[] = {0x00};
inline constexpr uint8_t kJD9365Data14[] = {0x00};
inline constexpr uint8_t kJD9365Data15[] = {0xAF};
inline constexpr uint8_t kJD9365Data16[] = {0x00};
inline constexpr uint8_t kJD9365Data17[] = {0x26};
inline constexpr uint8_t kJD9365Data18[] = {0x09};
inline constexpr uint8_t kJD9365Data19[] = {0x04};
inline constexpr uint8_t kJD9365Data20[] = {0x00};
inline constexpr uint8_t kJD9365Data21[] = {0x01};
inline constexpr uint8_t kJD9365Data22[] = {0x78};
inline constexpr uint8_t kJD9365Data23[] = {0xFF};
inline constexpr uint8_t kJD9365Data24[] = {0xFF};
inline constexpr uint8_t kJD9365Data25[] = {0x7F};
inline constexpr uint8_t kJD9365Data26[] = {0x06};
inline constexpr uint8_t kJD9365Data27[] = {0xA0};
inline constexpr uint8_t kJD9365Data28[] = {0x81};
inline constexpr uint8_t kJD9365Data29[] = {0x1E};
inline constexpr uint8_t kJD9365Data30[] = {0x0D};
inline constexpr uint8_t kJD9365Data31[] = {0x28};
inline constexpr uint8_t kJD9365Data32[] = {0x02};
inline constexpr uint8_t kJD9365Data33[] = {0x69};
inline constexpr uint8_t kJD9365Data34[] = {0x0A};
inline constexpr uint8_t kJD9365Data35[] = {0x2A};
inline constexpr uint8_t kJD9365Data36[] = {0x17};
inline constexpr uint8_t kJD9365Data37[] = {0x7F};
inline constexpr uint8_t kJD9365Data38[] = {0x6A};
inline constexpr uint8_t kJD9365Data39[] = {0x5B};
inline constexpr uint8_t kJD9365Data40[] = {0x4F};
inline constexpr uint8_t kJD9365Data41[] = {0x4A};
inline constexpr uint8_t kJD9365Data42[] = {0x3D};
inline constexpr uint8_t kJD9365Data43[] = {0x41};
inline constexpr uint8_t kJD9365Data44[] = {0x2A};
inline constexpr uint8_t kJD9365Data45[] = {0x44};
inline constexpr uint8_t kJD9365Data46[] = {0x43};
inline constexpr uint8_t kJD9365Data47[] = {0x44};
inline constexpr uint8_t kJD9365Data48[] = {0x62};
inline constexpr uint8_t kJD9365Data49[] = {0x52};
inline constexpr uint8_t kJD9365Data50[] = {0x59};
inline constexpr uint8_t kJD9365Data51[] = {0x4C};
inline constexpr uint8_t kJD9365Data52[] = {0x48};
inline constexpr uint8_t kJD9365Data53[] = {0x3A};
inline constexpr uint8_t kJD9365Data54[] = {0x26};
inline constexpr uint8_t kJD9365Data55[] = {0x00};
inline constexpr uint8_t kJD9365Data56[] = {0x7F};
inline constexpr uint8_t kJD9365Data57[] = {0x6A};
inline constexpr uint8_t kJD9365Data58[] = {0x5B};
inline constexpr uint8_t kJD9365Data59[] = {0x4F};
inline constexpr uint8_t kJD9365Data60[] = {0x4A};
inline constexpr uint8_t kJD9365Data61[] = {0x3D};
inline constexpr uint8_t kJD9365Data62[] = {0x41};
inline constexpr uint8_t kJD9365Data63[] = {0x2A};
inline constexpr uint8_t kJD9365Data64[] = {0x44};
inline constexpr uint8_t kJD9365Data65[] = {0x43};
inline constexpr uint8_t kJD9365Data66[] = {0x44};
inline constexpr uint8_t kJD9365Data67[] = {0x62};
inline constexpr uint8_t kJD9365Data68[] = {0x52};
inline constexpr uint8_t kJD9365Data69[] = {0x59};
inline constexpr uint8_t kJD9365Data70[] = {0x4C};
inline constexpr uint8_t kJD9365Data71[] = {0x48};
inline constexpr uint8_t kJD9365Data72[] = {0x3A};
inline constexpr uint8_t kJD9365Data73[] = {0x26};
inline constexpr uint8_t kJD9365Data74[] = {0x00};
inline constexpr uint8_t kJD9365Data75[] = {0x02};
inline constexpr uint8_t kJD9365Data76[] = {0x42};
inline constexpr uint8_t kJD9365Data77[] = {0x42};
inline constexpr uint8_t kJD9365Data78[] = {0x40};
inline constexpr uint8_t kJD9365Data79[] = {0x40};
inline constexpr uint8_t kJD9365Data80[] = {0x5E};
inline constexpr uint8_t kJD9365Data81[] = {0x5E};
inline constexpr uint8_t kJD9365Data82[] = {0x5F};
inline constexpr uint8_t kJD9365Data83[] = {0x5F};
inline constexpr uint8_t kJD9365Data84[] = {0x5F};
inline constexpr uint8_t kJD9365Data85[] = {0x57};
inline constexpr uint8_t kJD9365Data86[] = {0x57};
inline constexpr uint8_t kJD9365Data87[] = {0x77};
inline constexpr uint8_t kJD9365Data88[] = {0x77};
inline constexpr uint8_t kJD9365Data89[] = {0x47};
inline constexpr uint8_t kJD9365Data90[] = {0x47};
inline constexpr uint8_t kJD9365Data91[] = {0x45};
inline constexpr uint8_t kJD9365Data92[] = {0x45};
inline constexpr uint8_t kJD9365Data93[] = {0x4B};
inline constexpr uint8_t kJD9365Data94[] = {0x4B};
inline constexpr uint8_t kJD9365Data95[] = {0x49};
inline constexpr uint8_t kJD9365Data96[] = {0x49};
inline constexpr uint8_t kJD9365Data97[] = {0x5F};
inline constexpr uint8_t kJD9365Data98[] = {0x41};
inline constexpr uint8_t kJD9365Data99[] = {0x41};
inline constexpr uint8_t kJD9365Data100[] = {0x40};
inline constexpr uint8_t kJD9365Data101[] = {0x40};
inline constexpr uint8_t kJD9365Data102[] = {0x5E};
inline constexpr uint8_t kJD9365Data103[] = {0x5E};
inline constexpr uint8_t kJD9365Data104[] = {0x5F};
inline constexpr uint8_t kJD9365Data105[] = {0x5F};
inline constexpr uint8_t kJD9365Data106[] = {0x5F};
inline constexpr uint8_t kJD9365Data107[] = {0x57};
inline constexpr uint8_t kJD9365Data108[] = {0x57};
inline constexpr uint8_t kJD9365Data109[] = {0x77};
inline constexpr uint8_t kJD9365Data110[] = {0x77};
inline constexpr uint8_t kJD9365Data111[] = {0x46};
inline constexpr uint8_t kJD9365Data112[] = {0x46};
inline constexpr uint8_t kJD9365Data113[] = {0x44};
inline constexpr uint8_t kJD9365Data114[] = {0x44};
inline constexpr uint8_t kJD9365Data115[] = {0x4A};
inline constexpr uint8_t kJD9365Data116[] = {0x4A};
inline constexpr uint8_t kJD9365Data117[] = {0x48};
inline constexpr uint8_t kJD9365Data118[] = {0x48};
inline constexpr uint8_t kJD9365Data119[] = {0x5F};
inline constexpr uint8_t kJD9365Data120[] = {0x01};
inline constexpr uint8_t kJD9365Data121[] = {0x01};
inline constexpr uint8_t kJD9365Data122[] = {0x00};
inline constexpr uint8_t kJD9365Data123[] = {0x00};
inline constexpr uint8_t kJD9365Data124[] = {0x1F};
inline constexpr uint8_t kJD9365Data125[] = {0x1F};
inline constexpr uint8_t kJD9365Data126[] = {0x1E};
inline constexpr uint8_t kJD9365Data127[] = {0x1E};
inline constexpr uint8_t kJD9365Data128[] = {0x1F};
inline constexpr uint8_t kJD9365Data129[] = {0x17};
inline constexpr uint8_t kJD9365Data130[] = {0x17};
inline constexpr uint8_t kJD9365Data131[] = {0x37};
inline constexpr uint8_t kJD9365Data132[] = {0x37};
inline constexpr uint8_t kJD9365Data133[] = {0x08};
inline constexpr uint8_t kJD9365Data134[] = {0x08};
inline constexpr uint8_t kJD9365Data135[] = {0x0A};
inline constexpr uint8_t kJD9365Data136[] = {0x0A};
inline constexpr uint8_t kJD9365Data137[] = {0x04};
inline constexpr uint8_t kJD9365Data138[] = {0x04};
inline constexpr uint8_t kJD9365Data139[] = {0x06};
inline constexpr uint8_t kJD9365Data140[] = {0x06};
inline constexpr uint8_t kJD9365Data141[] = {0x1F};
inline constexpr uint8_t kJD9365Data142[] = {0x02};
inline constexpr uint8_t kJD9365Data143[] = {0x02};
inline constexpr uint8_t kJD9365Data144[] = {0x00};
inline constexpr uint8_t kJD9365Data145[] = {0x00};
inline constexpr uint8_t kJD9365Data146[] = {0x1F};
inline constexpr uint8_t kJD9365Data147[] = {0x1F};
inline constexpr uint8_t kJD9365Data148[] = {0x1E};
inline constexpr uint8_t kJD9365Data149[] = {0x1E};
inline constexpr uint8_t kJD9365Data150[] = {0x1F};
inline constexpr uint8_t kJD9365Data151[] = {0x17};
inline constexpr uint8_t kJD9365Data152[] = {0x17};
inline constexpr uint8_t kJD9365Data153[] = {0x37};
inline constexpr uint8_t kJD9365Data154[] = {0x37};
inline constexpr uint8_t kJD9365Data155[] = {0x09};
inline constexpr uint8_t kJD9365Data156[] = {0x09};
inline constexpr uint8_t kJD9365Data157[] = {0x0B};
inline constexpr uint8_t kJD9365Data158[] = {0x0B};
inline constexpr uint8_t kJD9365Data159[] = {0x05};
inline constexpr uint8_t kJD9365Data160[] = {0x05};
inline constexpr uint8_t kJD9365Data161[] = {0x07};
inline constexpr uint8_t kJD9365Data162[] = {0x07};
inline constexpr uint8_t kJD9365Data163[] = {0x1F};
inline constexpr uint8_t kJD9365Data164[] = {0x40};
inline constexpr uint8_t kJD9365Data165[] = {0x30};
inline constexpr uint8_t kJD9365Data166[] = {0x00};
inline constexpr uint8_t kJD9365Data167[] = {0x34};
inline constexpr uint8_t kJD9365Data168[] = {0x05};
inline constexpr uint8_t kJD9365Data169[] = {0x02};
inline constexpr uint8_t kJD9365Data170[] = {0x00};
inline constexpr uint8_t kJD9365Data171[] = {0x6A};
inline constexpr uint8_t kJD9365Data172[] = {0x73};
inline constexpr uint8_t kJD9365Data173[] = {0x07};
inline constexpr uint8_t kJD9365Data174[] = {0x08};
inline constexpr uint8_t kJD9365Data175[] = {0x6A};
inline constexpr uint8_t kJD9365Data176[] = {0x08};
inline constexpr uint8_t kJD9365Data177[] = {0x00};
inline constexpr uint8_t kJD9365Data178[] = {0x00};
inline constexpr uint8_t kJD9365Data179[] = {0x00};
inline constexpr uint8_t kJD9365Data180[] = {0x88};
inline constexpr uint8_t kJD9365Data181[] = {0xFF};
inline constexpr uint8_t kJD9365Data182[] = {0xDD};
inline constexpr uint8_t kJD9365Data183[] = {0x2C};
inline constexpr uint8_t kJD9365Data184[] = {0x15};
inline constexpr uint8_t kJD9365Data185[] = {0x17};
inline constexpr uint8_t kJD9365Data186[] = {0x14};
inline constexpr uint8_t kJD9365Data187[] = {0x82};
inline constexpr uint8_t kJD9365Data188[] = {0x04};
inline constexpr uint8_t kJD9365Data189[] = {0x0E};
inline constexpr uint8_t kJD9365Data190[] = {0xB3};
inline constexpr uint8_t kJD9365Data191[] = {0x61};
inline constexpr uint8_t kJD9365Data192[] = {0x48};
inline constexpr uint8_t kJD9365Data193[] = {0x58};
inline constexpr uint8_t kJD9365Data194[] = {0x0F};
inline constexpr uint8_t kJD9365Data195[] = {0x00};
inline constexpr uint8_t kJD9365Data196[] = {0x02};
inline constexpr uint8_t kJD9365Data197[] = {0x0C};
inline constexpr uint8_t kJD9365Data198[] = {0x00};
inline constexpr uint8_t kJD9365Data199[] = {0x00};
inline constexpr jd9365_lcd_init_cmd_t waveshare_p4_jd9365_init_cmds[] = {
    {0xE0, kJD9365Data0, 1, 0},
    {0xE1, kJD9365Data1, 1, 0},
    {0xE2, kJD9365Data2, 1, 0},
    {0xE3, kJD9365Data3, 1, 0},
    {0x80, kJD9365Data4, 1, 0},
    {0xE0, kJD9365Data5, 1, 0},
    {0x00, kJD9365Data6, 1, 0},
    {0x01, kJD9365Data7, 1, 0},
    {0x03, kJD9365Data8, 1, 0},
    {0x04, kJD9365Data9, 1, 0},
    {0x0C, kJD9365Data10, 1, 0},
    {0x17, kJD9365Data11, 1, 0},
    {0x18, kJD9365Data12, 1, 0},
    {0x19, kJD9365Data13, 1, 0},
    {0x1A, kJD9365Data14, 1, 0},
    {0x1B, kJD9365Data15, 1, 0},
    {0x1C, kJD9365Data16, 1, 0},
    {0x35, kJD9365Data17, 1, 0},
    {0x37, kJD9365Data18, 1, 0},
    {0x38, kJD9365Data19, 1, 0},
    {0x39, kJD9365Data20, 1, 0},
    {0x3A, kJD9365Data21, 1, 0},
    {0x3C, kJD9365Data22, 1, 0},
    {0x3D, kJD9365Data23, 1, 0},
    {0x3E, kJD9365Data24, 1, 0},
    {0x3F, kJD9365Data25, 1, 0},
    {0x40, kJD9365Data26, 1, 0},
    {0x41, kJD9365Data27, 1, 0},
    {0x42, kJD9365Data28, 1, 0},
    {0x43, kJD9365Data29, 1, 0},
    {0x44, kJD9365Data30, 1, 0},
    {0x45, kJD9365Data31, 1, 0},
    {0x55, kJD9365Data32, 1, 0},
    {0x57, kJD9365Data33, 1, 0},
    {0x59, kJD9365Data34, 1, 0},
    {0x5A, kJD9365Data35, 1, 0},
    {0x5B, kJD9365Data36, 1, 0},
    {0x5D, kJD9365Data37, 1, 0},
    {0x5E, kJD9365Data38, 1, 0},
    {0x5F, kJD9365Data39, 1, 0},
    {0x60, kJD9365Data40, 1, 0},
    {0x61, kJD9365Data41, 1, 0},
    {0x62, kJD9365Data42, 1, 0},
    {0x63, kJD9365Data43, 1, 0},
    {0x64, kJD9365Data44, 1, 0},
    {0x65, kJD9365Data45, 1, 0},
    {0x66, kJD9365Data46, 1, 0},
    {0x67, kJD9365Data47, 1, 0},
    {0x68, kJD9365Data48, 1, 0},
    {0x69, kJD9365Data49, 1, 0},
    {0x6A, kJD9365Data50, 1, 0},
    {0x6B, kJD9365Data51, 1, 0},
    {0x6C, kJD9365Data52, 1, 0},
    {0x6D, kJD9365Data53, 1, 0},
    {0x6E, kJD9365Data54, 1, 0},
    {0x6F, kJD9365Data55, 1, 0},
    {0x70, kJD9365Data56, 1, 0},
    {0x71, kJD9365Data57, 1, 0},
    {0x72, kJD9365Data58, 1, 0},
    {0x73, kJD9365Data59, 1, 0},
    {0x74, kJD9365Data60, 1, 0},
    {0x75, kJD9365Data61, 1, 0},
    {0x76, kJD9365Data62, 1, 0},
    {0x77, kJD9365Data63, 1, 0},
    {0x78, kJD9365Data64, 1, 0},
    {0x79, kJD9365Data65, 1, 0},
    {0x7A, kJD9365Data66, 1, 0},
    {0x7B, kJD9365Data67, 1, 0},
    {0x7C, kJD9365Data68, 1, 0},
    {0x7D, kJD9365Data69, 1, 0},
    {0x7E, kJD9365Data70, 1, 0},
    {0x7F, kJD9365Data71, 1, 0},
    {0x80, kJD9365Data72, 1, 0},
    {0x81, kJD9365Data73, 1, 0},
    {0x82, kJD9365Data74, 1, 0},
    {0xE0, kJD9365Data75, 1, 0},
    {0x00, kJD9365Data76, 1, 0},
    {0x01, kJD9365Data77, 1, 0},
    {0x02, kJD9365Data78, 1, 0},
    {0x03, kJD9365Data79, 1, 0},
    {0x04, kJD9365Data80, 1, 0},
    {0x05, kJD9365Data81, 1, 0},
    {0x06, kJD9365Data82, 1, 0},
    {0x07, kJD9365Data83, 1, 0},
    {0x08, kJD9365Data84, 1, 0},
    {0x09, kJD9365Data85, 1, 0},
    {0x0A, kJD9365Data86, 1, 0},
    {0x0B, kJD9365Data87, 1, 0},
    {0x0C, kJD9365Data88, 1, 0},
    {0x0D, kJD9365Data89, 1, 0},
    {0x0E, kJD9365Data90, 1, 0},
    {0x0F, kJD9365Data91, 1, 0},
    {0x10, kJD9365Data92, 1, 0},
    {0x11, kJD9365Data93, 1, 0},
    {0x12, kJD9365Data94, 1, 0},
    {0x13, kJD9365Data95, 1, 0},
    {0x14, kJD9365Data96, 1, 0},
    {0x15, kJD9365Data97, 1, 0},
    {0x16, kJD9365Data98, 1, 0},
    {0x17, kJD9365Data99, 1, 0},
    {0x18, kJD9365Data100, 1, 0},
    {0x19, kJD9365Data101, 1, 0},
    {0x1A, kJD9365Data102, 1, 0},
    {0x1B, kJD9365Data103, 1, 0},
    {0x1C, kJD9365Data104, 1, 0},
    {0x1D, kJD9365Data105, 1, 0},
    {0x1E, kJD9365Data106, 1, 0},
    {0x1F, kJD9365Data107, 1, 0},
    {0x20, kJD9365Data108, 1, 0},
    {0x21, kJD9365Data109, 1, 0},
    {0x22, kJD9365Data110, 1, 0},
    {0x23, kJD9365Data111, 1, 0},
    {0x24, kJD9365Data112, 1, 0},
    {0x25, kJD9365Data113, 1, 0},
    {0x26, kJD9365Data114, 1, 0},
    {0x27, kJD9365Data115, 1, 0},
    {0x28, kJD9365Data116, 1, 0},
    {0x29, kJD9365Data117, 1, 0},
    {0x2A, kJD9365Data118, 1, 0},
    {0x2B, kJD9365Data119, 1, 0},
    {0x2C, kJD9365Data120, 1, 0},
    {0x2D, kJD9365Data121, 1, 0},
    {0x2E, kJD9365Data122, 1, 0},
    {0x2F, kJD9365Data123, 1, 0},
    {0x30, kJD9365Data124, 1, 0},
    {0x31, kJD9365Data125, 1, 0},
    {0x32, kJD9365Data126, 1, 0},
    {0x33, kJD9365Data127, 1, 0},
    {0x34, kJD9365Data128, 1, 0},
    {0x35, kJD9365Data129, 1, 0},
    {0x36, kJD9365Data130, 1, 0},
    {0x37, kJD9365Data131, 1, 0},
    {0x38, kJD9365Data132, 1, 0},
    {0x39, kJD9365Data133, 1, 0},
    {0x3A, kJD9365Data134, 1, 0},
    {0x3B, kJD9365Data135, 1, 0},
    {0x3C, kJD9365Data136, 1, 0},
    {0x3D, kJD9365Data137, 1, 0},
    {0x3E, kJD9365Data138, 1, 0},
    {0x3F, kJD9365Data139, 1, 0},
    {0x40, kJD9365Data140, 1, 0},
    {0x41, kJD9365Data141, 1, 0},
    {0x42, kJD9365Data142, 1, 0},
    {0x43, kJD9365Data143, 1, 0},
    {0x44, kJD9365Data144, 1, 0},
    {0x45, kJD9365Data145, 1, 0},
    {0x46, kJD9365Data146, 1, 0},
    {0x47, kJD9365Data147, 1, 0},
    {0x48, kJD9365Data148, 1, 0},
    {0x49, kJD9365Data149, 1, 0},
    {0x4A, kJD9365Data150, 1, 0},
    {0x4B, kJD9365Data151, 1, 0},
    {0x4C, kJD9365Data152, 1, 0},
    {0x4D, kJD9365Data153, 1, 0},
    {0x4E, kJD9365Data154, 1, 0},
    {0x4F, kJD9365Data155, 1, 0},
    {0x50, kJD9365Data156, 1, 0},
    {0x51, kJD9365Data157, 1, 0},
    {0x52, kJD9365Data158, 1, 0},
    {0x53, kJD9365Data159, 1, 0},
    {0x54, kJD9365Data160, 1, 0},
    {0x55, kJD9365Data161, 1, 0},
    {0x56, kJD9365Data162, 1, 0},
    {0x57, kJD9365Data163, 1, 0},
    {0x58, kJD9365Data164, 1, 0},
    {0x5B, kJD9365Data165, 1, 0},
    {0x5C, kJD9365Data166, 1, 0},
    {0x5D, kJD9365Data167, 1, 0},
    {0x5E, kJD9365Data168, 1, 0},
    {0x5F, kJD9365Data169, 1, 0},
    {0x63, kJD9365Data170, 1, 0},
    {0x64, kJD9365Data171, 1, 0},
    {0x67, kJD9365Data172, 1, 0},
    {0x68, kJD9365Data173, 1, 0},
    {0x69, kJD9365Data174, 1, 0},
    {0x6A, kJD9365Data175, 1, 0},
    {0x6B, kJD9365Data176, 1, 0},
    {0x6C, kJD9365Data177, 1, 0},
    {0x6D, kJD9365Data178, 1, 0},
    {0x6E, kJD9365Data179, 1, 0},
    {0x6F, kJD9365Data180, 1, 0},
    {0x75, kJD9365Data181, 1, 0},
    {0x77, kJD9365Data182, 1, 0},
    {0x78, kJD9365Data183, 1, 0},
    {0x79, kJD9365Data184, 1, 0},
    {0x7A, kJD9365Data185, 1, 0},
    {0x7D, kJD9365Data186, 1, 0},
    {0x7E, kJD9365Data187, 1, 0},
    {0xE0, kJD9365Data188, 1, 0},
    {0x00, kJD9365Data189, 1, 0},
    {0x02, kJD9365Data190, 1, 0},
    {0x09, kJD9365Data191, 1, 0},
    {0x0E, kJD9365Data192, 1, 0},
    {0x37, kJD9365Data193, 1, 0},
    {0x2B, kJD9365Data194, 1, 0},
    {0xE0, kJD9365Data195, 1, 0},
    {0xE6, kJD9365Data196, 1, 0},
    {0xE7, kJD9365Data197, 1, 0},
    {0x11, kJD9365Data198, 1, 120},
    {0x29, kJD9365Data199, 1, 20},
};

#else
#error "Select a pinned JD9365 panel command table"
#endif
