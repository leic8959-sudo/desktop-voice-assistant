#pragma once
#include <cstdint>
#include "esp_lcd_jd9365.h"

// Pinned Nano 10.1A table; do not substitute another 800x1280 panel.
// 78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e
// MIT license: third_party/xiaozhi_p4_panels/LICENSE

inline constexpr uint8_t kNanoData0[] = {0x00};
inline constexpr uint8_t kNanoData1[] = {0x93};
inline constexpr uint8_t kNanoData2[] = {0x65};
inline constexpr uint8_t kNanoData3[] = {0xF8};
inline constexpr uint8_t kNanoData4[] = {0x01};
inline constexpr uint8_t kNanoData5[] = {0x01};
inline constexpr uint8_t kNanoData6[] = {0x00};
inline constexpr uint8_t kNanoData7[] = {0x38};
inline constexpr uint8_t kNanoData8[] = {0x10};
inline constexpr uint8_t kNanoData9[] = {0x38};
inline constexpr uint8_t kNanoData10[] = {0x74};
inline constexpr uint8_t kNanoData11[] = {0x00};
inline constexpr uint8_t kNanoData12[] = {0xAF};
inline constexpr uint8_t kNanoData13[] = {0x00};
inline constexpr uint8_t kNanoData14[] = {0x00};
inline constexpr uint8_t kNanoData15[] = {0xAF};
inline constexpr uint8_t kNanoData16[] = {0x00};
inline constexpr uint8_t kNanoData17[] = {0x26};
inline constexpr uint8_t kNanoData18[] = {0x09};
inline constexpr uint8_t kNanoData19[] = {0x04};
inline constexpr uint8_t kNanoData20[] = {0x00};
inline constexpr uint8_t kNanoData21[] = {0x01};
inline constexpr uint8_t kNanoData22[] = {0x78};
inline constexpr uint8_t kNanoData23[] = {0xFF};
inline constexpr uint8_t kNanoData24[] = {0xFF};
inline constexpr uint8_t kNanoData25[] = {0x7F};
inline constexpr uint8_t kNanoData26[] = {0x06};
inline constexpr uint8_t kNanoData27[] = {0xA0};
inline constexpr uint8_t kNanoData28[] = {0x81};
inline constexpr uint8_t kNanoData29[] = {0x1E};
inline constexpr uint8_t kNanoData30[] = {0x0D};
inline constexpr uint8_t kNanoData31[] = {0x28};
inline constexpr uint8_t kNanoData32[] = {0x02};
inline constexpr uint8_t kNanoData33[] = {0x69};
inline constexpr uint8_t kNanoData34[] = {0x0A};
inline constexpr uint8_t kNanoData35[] = {0x2A};
inline constexpr uint8_t kNanoData36[] = {0x17};
inline constexpr uint8_t kNanoData37[] = {0x7F};
inline constexpr uint8_t kNanoData38[] = {0x6A};
inline constexpr uint8_t kNanoData39[] = {0x5B};
inline constexpr uint8_t kNanoData40[] = {0x4F};
inline constexpr uint8_t kNanoData41[] = {0x4A};
inline constexpr uint8_t kNanoData42[] = {0x3D};
inline constexpr uint8_t kNanoData43[] = {0x41};
inline constexpr uint8_t kNanoData44[] = {0x2A};
inline constexpr uint8_t kNanoData45[] = {0x44};
inline constexpr uint8_t kNanoData46[] = {0x43};
inline constexpr uint8_t kNanoData47[] = {0x44};
inline constexpr uint8_t kNanoData48[] = {0x62};
inline constexpr uint8_t kNanoData49[] = {0x52};
inline constexpr uint8_t kNanoData50[] = {0x59};
inline constexpr uint8_t kNanoData51[] = {0x4C};
inline constexpr uint8_t kNanoData52[] = {0x48};
inline constexpr uint8_t kNanoData53[] = {0x3A};
inline constexpr uint8_t kNanoData54[] = {0x26};
inline constexpr uint8_t kNanoData55[] = {0x00};
inline constexpr uint8_t kNanoData56[] = {0x7F};
inline constexpr uint8_t kNanoData57[] = {0x6A};
inline constexpr uint8_t kNanoData58[] = {0x5B};
inline constexpr uint8_t kNanoData59[] = {0x4F};
inline constexpr uint8_t kNanoData60[] = {0x4A};
inline constexpr uint8_t kNanoData61[] = {0x3D};
inline constexpr uint8_t kNanoData62[] = {0x41};
inline constexpr uint8_t kNanoData63[] = {0x2A};
inline constexpr uint8_t kNanoData64[] = {0x44};
inline constexpr uint8_t kNanoData65[] = {0x43};
inline constexpr uint8_t kNanoData66[] = {0x44};
inline constexpr uint8_t kNanoData67[] = {0x62};
inline constexpr uint8_t kNanoData68[] = {0x52};
inline constexpr uint8_t kNanoData69[] = {0x59};
inline constexpr uint8_t kNanoData70[] = {0x4C};
inline constexpr uint8_t kNanoData71[] = {0x48};
inline constexpr uint8_t kNanoData72[] = {0x3A};
inline constexpr uint8_t kNanoData73[] = {0x26};
inline constexpr uint8_t kNanoData74[] = {0x00};
inline constexpr uint8_t kNanoData75[] = {0x02};
inline constexpr uint8_t kNanoData76[] = {0x42};
inline constexpr uint8_t kNanoData77[] = {0x42};
inline constexpr uint8_t kNanoData78[] = {0x40};
inline constexpr uint8_t kNanoData79[] = {0x40};
inline constexpr uint8_t kNanoData80[] = {0x5E};
inline constexpr uint8_t kNanoData81[] = {0x5E};
inline constexpr uint8_t kNanoData82[] = {0x5F};
inline constexpr uint8_t kNanoData83[] = {0x5F};
inline constexpr uint8_t kNanoData84[] = {0x5F};
inline constexpr uint8_t kNanoData85[] = {0x57};
inline constexpr uint8_t kNanoData86[] = {0x57};
inline constexpr uint8_t kNanoData87[] = {0x77};
inline constexpr uint8_t kNanoData88[] = {0x77};
inline constexpr uint8_t kNanoData89[] = {0x47};
inline constexpr uint8_t kNanoData90[] = {0x47};
inline constexpr uint8_t kNanoData91[] = {0x45};
inline constexpr uint8_t kNanoData92[] = {0x45};
inline constexpr uint8_t kNanoData93[] = {0x4B};
inline constexpr uint8_t kNanoData94[] = {0x4B};
inline constexpr uint8_t kNanoData95[] = {0x49};
inline constexpr uint8_t kNanoData96[] = {0x49};
inline constexpr uint8_t kNanoData97[] = {0x5F};
inline constexpr uint8_t kNanoData98[] = {0x41};
inline constexpr uint8_t kNanoData99[] = {0x41};
inline constexpr uint8_t kNanoData100[] = {0x40};
inline constexpr uint8_t kNanoData101[] = {0x40};
inline constexpr uint8_t kNanoData102[] = {0x5E};
inline constexpr uint8_t kNanoData103[] = {0x5E};
inline constexpr uint8_t kNanoData104[] = {0x5F};
inline constexpr uint8_t kNanoData105[] = {0x5F};
inline constexpr uint8_t kNanoData106[] = {0x5F};
inline constexpr uint8_t kNanoData107[] = {0x57};
inline constexpr uint8_t kNanoData108[] = {0x57};
inline constexpr uint8_t kNanoData109[] = {0x77};
inline constexpr uint8_t kNanoData110[] = {0x77};
inline constexpr uint8_t kNanoData111[] = {0x46};
inline constexpr uint8_t kNanoData112[] = {0x46};
inline constexpr uint8_t kNanoData113[] = {0x44};
inline constexpr uint8_t kNanoData114[] = {0x44};
inline constexpr uint8_t kNanoData115[] = {0x4A};
inline constexpr uint8_t kNanoData116[] = {0x4A};
inline constexpr uint8_t kNanoData117[] = {0x48};
inline constexpr uint8_t kNanoData118[] = {0x48};
inline constexpr uint8_t kNanoData119[] = {0x5F};
inline constexpr uint8_t kNanoData120[] = {0x01};
inline constexpr uint8_t kNanoData121[] = {0x01};
inline constexpr uint8_t kNanoData122[] = {0x00};
inline constexpr uint8_t kNanoData123[] = {0x00};
inline constexpr uint8_t kNanoData124[] = {0x1F};
inline constexpr uint8_t kNanoData125[] = {0x1F};
inline constexpr uint8_t kNanoData126[] = {0x1E};
inline constexpr uint8_t kNanoData127[] = {0x1E};
inline constexpr uint8_t kNanoData128[] = {0x1F};
inline constexpr uint8_t kNanoData129[] = {0x17};
inline constexpr uint8_t kNanoData130[] = {0x17};
inline constexpr uint8_t kNanoData131[] = {0x37};
inline constexpr uint8_t kNanoData132[] = {0x37};
inline constexpr uint8_t kNanoData133[] = {0x08};
inline constexpr uint8_t kNanoData134[] = {0x08};
inline constexpr uint8_t kNanoData135[] = {0x0A};
inline constexpr uint8_t kNanoData136[] = {0x0A};
inline constexpr uint8_t kNanoData137[] = {0x04};
inline constexpr uint8_t kNanoData138[] = {0x04};
inline constexpr uint8_t kNanoData139[] = {0x06};
inline constexpr uint8_t kNanoData140[] = {0x06};
inline constexpr uint8_t kNanoData141[] = {0x1F};
inline constexpr uint8_t kNanoData142[] = {0x02};
inline constexpr uint8_t kNanoData143[] = {0x02};
inline constexpr uint8_t kNanoData144[] = {0x00};
inline constexpr uint8_t kNanoData145[] = {0x00};
inline constexpr uint8_t kNanoData146[] = {0x1F};
inline constexpr uint8_t kNanoData147[] = {0x1F};
inline constexpr uint8_t kNanoData148[] = {0x1E};
inline constexpr uint8_t kNanoData149[] = {0x1E};
inline constexpr uint8_t kNanoData150[] = {0x1F};
inline constexpr uint8_t kNanoData151[] = {0x17};
inline constexpr uint8_t kNanoData152[] = {0x17};
inline constexpr uint8_t kNanoData153[] = {0x37};
inline constexpr uint8_t kNanoData154[] = {0x37};
inline constexpr uint8_t kNanoData155[] = {0x09};
inline constexpr uint8_t kNanoData156[] = {0x09};
inline constexpr uint8_t kNanoData157[] = {0x0B};
inline constexpr uint8_t kNanoData158[] = {0x0B};
inline constexpr uint8_t kNanoData159[] = {0x05};
inline constexpr uint8_t kNanoData160[] = {0x05};
inline constexpr uint8_t kNanoData161[] = {0x07};
inline constexpr uint8_t kNanoData162[] = {0x07};
inline constexpr uint8_t kNanoData163[] = {0x1F};
inline constexpr uint8_t kNanoData164[] = {0x40};
inline constexpr uint8_t kNanoData165[] = {0x30};
inline constexpr uint8_t kNanoData166[] = {0x00};
inline constexpr uint8_t kNanoData167[] = {0x34};
inline constexpr uint8_t kNanoData168[] = {0x05};
inline constexpr uint8_t kNanoData169[] = {0x02};
inline constexpr uint8_t kNanoData170[] = {0x00};
inline constexpr uint8_t kNanoData171[] = {0x6A};
inline constexpr uint8_t kNanoData172[] = {0x73};
inline constexpr uint8_t kNanoData173[] = {0x07};
inline constexpr uint8_t kNanoData174[] = {0x08};
inline constexpr uint8_t kNanoData175[] = {0x6A};
inline constexpr uint8_t kNanoData176[] = {0x08};
inline constexpr uint8_t kNanoData177[] = {0x00};
inline constexpr uint8_t kNanoData178[] = {0x00};
inline constexpr uint8_t kNanoData179[] = {0x00};
inline constexpr uint8_t kNanoData180[] = {0x88};
inline constexpr uint8_t kNanoData181[] = {0xFF};
inline constexpr uint8_t kNanoData182[] = {0xDD};
inline constexpr uint8_t kNanoData183[] = {0x2C};
inline constexpr uint8_t kNanoData184[] = {0x15};
inline constexpr uint8_t kNanoData185[] = {0x17};
inline constexpr uint8_t kNanoData186[] = {0x14};
inline constexpr uint8_t kNanoData187[] = {0x82};
inline constexpr uint8_t kNanoData188[] = {0x04};
inline constexpr uint8_t kNanoData189[] = {0x0E};
inline constexpr uint8_t kNanoData190[] = {0xB3};
inline constexpr uint8_t kNanoData191[] = {0x61};
inline constexpr uint8_t kNanoData192[] = {0x48};
inline constexpr uint8_t kNanoData193[] = {0x58};
inline constexpr uint8_t kNanoData194[] = {0x0F};
inline constexpr uint8_t kNanoData195[] = {0x00};
inline constexpr uint8_t kNanoData196[] = {0x02};
inline constexpr uint8_t kNanoData197[] = {0x0C};
inline constexpr uint8_t kNanoData198[] = {0x00};
inline constexpr uint8_t kNanoData199[] = {0x00};
inline constexpr jd9365_lcd_init_cmd_t waveshare_p4_nano_init_cmds[] = {
    {0xE0, kNanoData0, 1, 0},
    {0xE1, kNanoData1, 1, 0},
    {0xE2, kNanoData2, 1, 0},
    {0xE3, kNanoData3, 1, 0},
    {0x80, kNanoData4, 1, 0},
    {0xE0, kNanoData5, 1, 0},
    {0x00, kNanoData6, 1, 0},
    {0x01, kNanoData7, 1, 0},
    {0x03, kNanoData8, 1, 0},
    {0x04, kNanoData9, 1, 0},
    {0x0C, kNanoData10, 1, 0},
    {0x17, kNanoData11, 1, 0},
    {0x18, kNanoData12, 1, 0},
    {0x19, kNanoData13, 1, 0},
    {0x1A, kNanoData14, 1, 0},
    {0x1B, kNanoData15, 1, 0},
    {0x1C, kNanoData16, 1, 0},
    {0x35, kNanoData17, 1, 0},
    {0x37, kNanoData18, 1, 0},
    {0x38, kNanoData19, 1, 0},
    {0x39, kNanoData20, 1, 0},
    {0x3A, kNanoData21, 1, 0},
    {0x3C, kNanoData22, 1, 0},
    {0x3D, kNanoData23, 1, 0},
    {0x3E, kNanoData24, 1, 0},
    {0x3F, kNanoData25, 1, 0},
    {0x40, kNanoData26, 1, 0},
    {0x41, kNanoData27, 1, 0},
    {0x42, kNanoData28, 1, 0},
    {0x43, kNanoData29, 1, 0},
    {0x44, kNanoData30, 1, 0},
    {0x45, kNanoData31, 1, 0},
    {0x55, kNanoData32, 1, 0},
    {0x57, kNanoData33, 1, 0},
    {0x59, kNanoData34, 1, 0},
    {0x5A, kNanoData35, 1, 0},
    {0x5B, kNanoData36, 1, 0},
    {0x5D, kNanoData37, 1, 0},
    {0x5E, kNanoData38, 1, 0},
    {0x5F, kNanoData39, 1, 0},
    {0x60, kNanoData40, 1, 0},
    {0x61, kNanoData41, 1, 0},
    {0x62, kNanoData42, 1, 0},
    {0x63, kNanoData43, 1, 0},
    {0x64, kNanoData44, 1, 0},
    {0x65, kNanoData45, 1, 0},
    {0x66, kNanoData46, 1, 0},
    {0x67, kNanoData47, 1, 0},
    {0x68, kNanoData48, 1, 0},
    {0x69, kNanoData49, 1, 0},
    {0x6A, kNanoData50, 1, 0},
    {0x6B, kNanoData51, 1, 0},
    {0x6C, kNanoData52, 1, 0},
    {0x6D, kNanoData53, 1, 0},
    {0x6E, kNanoData54, 1, 0},
    {0x6F, kNanoData55, 1, 0},
    {0x70, kNanoData56, 1, 0},
    {0x71, kNanoData57, 1, 0},
    {0x72, kNanoData58, 1, 0},
    {0x73, kNanoData59, 1, 0},
    {0x74, kNanoData60, 1, 0},
    {0x75, kNanoData61, 1, 0},
    {0x76, kNanoData62, 1, 0},
    {0x77, kNanoData63, 1, 0},
    {0x78, kNanoData64, 1, 0},
    {0x79, kNanoData65, 1, 0},
    {0x7A, kNanoData66, 1, 0},
    {0x7B, kNanoData67, 1, 0},
    {0x7C, kNanoData68, 1, 0},
    {0x7D, kNanoData69, 1, 0},
    {0x7E, kNanoData70, 1, 0},
    {0x7F, kNanoData71, 1, 0},
    {0x80, kNanoData72, 1, 0},
    {0x81, kNanoData73, 1, 0},
    {0x82, kNanoData74, 1, 0},
    {0xE0, kNanoData75, 1, 0},
    {0x00, kNanoData76, 1, 0},
    {0x01, kNanoData77, 1, 0},
    {0x02, kNanoData78, 1, 0},
    {0x03, kNanoData79, 1, 0},
    {0x04, kNanoData80, 1, 0},
    {0x05, kNanoData81, 1, 0},
    {0x06, kNanoData82, 1, 0},
    {0x07, kNanoData83, 1, 0},
    {0x08, kNanoData84, 1, 0},
    {0x09, kNanoData85, 1, 0},
    {0x0A, kNanoData86, 1, 0},
    {0x0B, kNanoData87, 1, 0},
    {0x0C, kNanoData88, 1, 0},
    {0x0D, kNanoData89, 1, 0},
    {0x0E, kNanoData90, 1, 0},
    {0x0F, kNanoData91, 1, 0},
    {0x10, kNanoData92, 1, 0},
    {0x11, kNanoData93, 1, 0},
    {0x12, kNanoData94, 1, 0},
    {0x13, kNanoData95, 1, 0},
    {0x14, kNanoData96, 1, 0},
    {0x15, kNanoData97, 1, 0},
    {0x16, kNanoData98, 1, 0},
    {0x17, kNanoData99, 1, 0},
    {0x18, kNanoData100, 1, 0},
    {0x19, kNanoData101, 1, 0},
    {0x1A, kNanoData102, 1, 0},
    {0x1B, kNanoData103, 1, 0},
    {0x1C, kNanoData104, 1, 0},
    {0x1D, kNanoData105, 1, 0},
    {0x1E, kNanoData106, 1, 0},
    {0x1F, kNanoData107, 1, 0},
    {0x20, kNanoData108, 1, 0},
    {0x21, kNanoData109, 1, 0},
    {0x22, kNanoData110, 1, 0},
    {0x23, kNanoData111, 1, 0},
    {0x24, kNanoData112, 1, 0},
    {0x25, kNanoData113, 1, 0},
    {0x26, kNanoData114, 1, 0},
    {0x27, kNanoData115, 1, 0},
    {0x28, kNanoData116, 1, 0},
    {0x29, kNanoData117, 1, 0},
    {0x2A, kNanoData118, 1, 0},
    {0x2B, kNanoData119, 1, 0},
    {0x2C, kNanoData120, 1, 0},
    {0x2D, kNanoData121, 1, 0},
    {0x2E, kNanoData122, 1, 0},
    {0x2F, kNanoData123, 1, 0},
    {0x30, kNanoData124, 1, 0},
    {0x31, kNanoData125, 1, 0},
    {0x32, kNanoData126, 1, 0},
    {0x33, kNanoData127, 1, 0},
    {0x34, kNanoData128, 1, 0},
    {0x35, kNanoData129, 1, 0},
    {0x36, kNanoData130, 1, 0},
    {0x37, kNanoData131, 1, 0},
    {0x38, kNanoData132, 1, 0},
    {0x39, kNanoData133, 1, 0},
    {0x3A, kNanoData134, 1, 0},
    {0x3B, kNanoData135, 1, 0},
    {0x3C, kNanoData136, 1, 0},
    {0x3D, kNanoData137, 1, 0},
    {0x3E, kNanoData138, 1, 0},
    {0x3F, kNanoData139, 1, 0},
    {0x40, kNanoData140, 1, 0},
    {0x41, kNanoData141, 1, 0},
    {0x42, kNanoData142, 1, 0},
    {0x43, kNanoData143, 1, 0},
    {0x44, kNanoData144, 1, 0},
    {0x45, kNanoData145, 1, 0},
    {0x46, kNanoData146, 1, 0},
    {0x47, kNanoData147, 1, 0},
    {0x48, kNanoData148, 1, 0},
    {0x49, kNanoData149, 1, 0},
    {0x4A, kNanoData150, 1, 0},
    {0x4B, kNanoData151, 1, 0},
    {0x4C, kNanoData152, 1, 0},
    {0x4D, kNanoData153, 1, 0},
    {0x4E, kNanoData154, 1, 0},
    {0x4F, kNanoData155, 1, 0},
    {0x50, kNanoData156, 1, 0},
    {0x51, kNanoData157, 1, 0},
    {0x52, kNanoData158, 1, 0},
    {0x53, kNanoData159, 1, 0},
    {0x54, kNanoData160, 1, 0},
    {0x55, kNanoData161, 1, 0},
    {0x56, kNanoData162, 1, 0},
    {0x57, kNanoData163, 1, 0},
    {0x58, kNanoData164, 1, 0},
    {0x5B, kNanoData165, 1, 0},
    {0x5C, kNanoData166, 1, 0},
    {0x5D, kNanoData167, 1, 0},
    {0x5E, kNanoData168, 1, 0},
    {0x5F, kNanoData169, 1, 0},
    {0x63, kNanoData170, 1, 0},
    {0x64, kNanoData171, 1, 0},
    {0x67, kNanoData172, 1, 0},
    {0x68, kNanoData173, 1, 0},
    {0x69, kNanoData174, 1, 0},
    {0x6A, kNanoData175, 1, 0},
    {0x6B, kNanoData176, 1, 0},
    {0x6C, kNanoData177, 1, 0},
    {0x6D, kNanoData178, 1, 0},
    {0x6E, kNanoData179, 1, 0},
    {0x6F, kNanoData180, 1, 0},
    {0x75, kNanoData181, 1, 0},
    {0x77, kNanoData182, 1, 0},
    {0x78, kNanoData183, 1, 0},
    {0x79, kNanoData184, 1, 0},
    {0x7A, kNanoData185, 1, 0},
    {0x7D, kNanoData186, 1, 0},
    {0x7E, kNanoData187, 1, 0},
    {0xE0, kNanoData188, 1, 0},
    {0x00, kNanoData189, 1, 0},
    {0x02, kNanoData190, 1, 0},
    {0x09, kNanoData191, 1, 0},
    {0x0E, kNanoData192, 1, 0},
    {0x37, kNanoData193, 1, 0},
    {0x2B, kNanoData194, 1, 0},
    {0xE0, kNanoData195, 1, 0},
    {0xE6, kNanoData196, 1, 0},
    {0xE7, kNanoData197, 1, 0},
    {0x11, kNanoData198, 1, 120},
    {0x29, kNanoData199, 1, 20},
};
