#pragma once
#include <cstdint>
#include "esp_lcd_ili9881c.h"

// Command bytes/counts/delays derived from the fixed upstream 4.3/7 panel branch.
// Source: 78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e
// See main/third_party/xiaozhi_p4_panels/LICENSE.

inline constexpr uint8_t kILI9881CData0[] = {0x98, 0x81, 0x03};
inline constexpr uint8_t kILI9881CData1[] = {0x00};
inline constexpr uint8_t kILI9881CData2[] = {0x00};
inline constexpr uint8_t kILI9881CData3[] = {0x73};
inline constexpr uint8_t kILI9881CData4[] = {0x00};
inline constexpr uint8_t kILI9881CData5[] = {0x00};
inline constexpr uint8_t kILI9881CData6[] = {0x0A};
inline constexpr uint8_t kILI9881CData7[] = {0x00};
inline constexpr uint8_t kILI9881CData8[] = {0x00};
inline constexpr uint8_t kILI9881CData9[] = {0x61};
inline constexpr uint8_t kILI9881CData10[] = {0x00};
inline constexpr uint8_t kILI9881CData11[] = {0x00};
inline constexpr uint8_t kILI9881CData12[] = {0x01};
inline constexpr uint8_t kILI9881CData13[] = {0x00};
inline constexpr uint8_t kILI9881CData14[] = {0x00};
inline constexpr uint8_t kILI9881CData15[] = {0x61};
inline constexpr uint8_t kILI9881CData16[] = {0x61};
inline constexpr uint8_t kILI9881CData17[] = {0x00};
inline constexpr uint8_t kILI9881CData18[] = {0x00};
inline constexpr uint8_t kILI9881CData19[] = {0x00};
inline constexpr uint8_t kILI9881CData20[] = {0x00};
inline constexpr uint8_t kILI9881CData21[] = {0x00};
inline constexpr uint8_t kILI9881CData22[] = {0x00};
inline constexpr uint8_t kILI9881CData23[] = {0x00};
inline constexpr uint8_t kILI9881CData24[] = {0x00};
inline constexpr uint8_t kILI9881CData25[] = {0x00};
inline constexpr uint8_t kILI9881CData26[] = {0x00};
inline constexpr uint8_t kILI9881CData27[] = {0x00};
inline constexpr uint8_t kILI9881CData28[] = {0x00};
inline constexpr uint8_t kILI9881CData29[] = {0x00};
inline constexpr uint8_t kILI9881CData30[] = {0x40};
inline constexpr uint8_t kILI9881CData31[] = {0x80};
inline constexpr uint8_t kILI9881CData32[] = {0x06};
inline constexpr uint8_t kILI9881CData33[] = {0x01};
inline constexpr uint8_t kILI9881CData34[] = {0x00};
inline constexpr uint8_t kILI9881CData35[] = {0x00};
inline constexpr uint8_t kILI9881CData36[] = {0x00};
inline constexpr uint8_t kILI9881CData37[] = {0x00};
inline constexpr uint8_t kILI9881CData38[] = {0x00};
inline constexpr uint8_t kILI9881CData39[] = {0x00};
inline constexpr uint8_t kILI9881CData40[] = {0x33};
inline constexpr uint8_t kILI9881CData41[] = {0x03};
inline constexpr uint8_t kILI9881CData42[] = {0x00};
inline constexpr uint8_t kILI9881CData43[] = {0x00};
inline constexpr uint8_t kILI9881CData44[] = {0x00};
inline constexpr uint8_t kILI9881CData45[] = {0x00};
inline constexpr uint8_t kILI9881CData46[] = {0x00};
inline constexpr uint8_t kILI9881CData47[] = {0x00};
inline constexpr uint8_t kILI9881CData48[] = {0x00};
inline constexpr uint8_t kILI9881CData49[] = {0x00};
inline constexpr uint8_t kILI9881CData50[] = {0x00};
inline constexpr uint8_t kILI9881CData51[] = {0x00};
inline constexpr uint8_t kILI9881CData52[] = {0x04};
inline constexpr uint8_t kILI9881CData53[] = {0x00};
inline constexpr uint8_t kILI9881CData54[] = {0x00};
inline constexpr uint8_t kILI9881CData55[] = {0x00};
inline constexpr uint8_t kILI9881CData56[] = {0x3C};
inline constexpr uint8_t kILI9881CData57[] = {0x00};
inline constexpr uint8_t kILI9881CData58[] = {0x00};
inline constexpr uint8_t kILI9881CData59[] = {0x00};
inline constexpr uint8_t kILI9881CData60[] = {0x00};
inline constexpr uint8_t kILI9881CData61[] = {0x00};
inline constexpr uint8_t kILI9881CData62[] = {0x00};
inline constexpr uint8_t kILI9881CData63[] = {0x00};
inline constexpr uint8_t kILI9881CData64[] = {0x00};
inline constexpr uint8_t kILI9881CData65[] = {0x00};
inline constexpr uint8_t kILI9881CData66[] = {0x00};
inline constexpr uint8_t kILI9881CData67[] = {0x00};
inline constexpr uint8_t kILI9881CData68[] = {0x00};
inline constexpr uint8_t kILI9881CData69[] = {0x10};
inline constexpr uint8_t kILI9881CData70[] = {0x32};
inline constexpr uint8_t kILI9881CData71[] = {0x54};
inline constexpr uint8_t kILI9881CData72[] = {0x76};
inline constexpr uint8_t kILI9881CData73[] = {0x98};
inline constexpr uint8_t kILI9881CData74[] = {0xBA};
inline constexpr uint8_t kILI9881CData75[] = {0x10};
inline constexpr uint8_t kILI9881CData76[] = {0x32};
inline constexpr uint8_t kILI9881CData77[] = {0x54};
inline constexpr uint8_t kILI9881CData78[] = {0x76};
inline constexpr uint8_t kILI9881CData79[] = {0x98};
inline constexpr uint8_t kILI9881CData80[] = {0xBA};
inline constexpr uint8_t kILI9881CData81[] = {0xDC};
inline constexpr uint8_t kILI9881CData82[] = {0xFE};
inline constexpr uint8_t kILI9881CData83[] = {0x00};
inline constexpr uint8_t kILI9881CData84[] = {0x0E};
inline constexpr uint8_t kILI9881CData85[] = {0x0F};
inline constexpr uint8_t kILI9881CData86[] = {0x0C};
inline constexpr uint8_t kILI9881CData87[] = {0x0D};
inline constexpr uint8_t kILI9881CData88[] = {0x06};
inline constexpr uint8_t kILI9881CData89[] = {0x07};
inline constexpr uint8_t kILI9881CData90[] = {0x02};
inline constexpr uint8_t kILI9881CData91[] = {0x02};
inline constexpr uint8_t kILI9881CData92[] = {0x02};
inline constexpr uint8_t kILI9881CData93[] = {0x02};
inline constexpr uint8_t kILI9881CData94[] = {0x01};
inline constexpr uint8_t kILI9881CData95[] = {0x00};
inline constexpr uint8_t kILI9881CData96[] = {0x02};
inline constexpr uint8_t kILI9881CData97[] = {0x15};
inline constexpr uint8_t kILI9881CData98[] = {0x14};
inline constexpr uint8_t kILI9881CData99[] = {0x02};
inline constexpr uint8_t kILI9881CData100[] = {0x02};
inline constexpr uint8_t kILI9881CData101[] = {0x02};
inline constexpr uint8_t kILI9881CData102[] = {0x02};
inline constexpr uint8_t kILI9881CData103[] = {0x02};
inline constexpr uint8_t kILI9881CData104[] = {0x02};
inline constexpr uint8_t kILI9881CData105[] = {0x02};
inline constexpr uint8_t kILI9881CData106[] = {0x0E};
inline constexpr uint8_t kILI9881CData107[] = {0x0F};
inline constexpr uint8_t kILI9881CData108[] = {0x0C};
inline constexpr uint8_t kILI9881CData109[] = {0x0D};
inline constexpr uint8_t kILI9881CData110[] = {0x06};
inline constexpr uint8_t kILI9881CData111[] = {0x07};
inline constexpr uint8_t kILI9881CData112[] = {0x02};
inline constexpr uint8_t kILI9881CData113[] = {0x02};
inline constexpr uint8_t kILI9881CData114[] = {0x02};
inline constexpr uint8_t kILI9881CData115[] = {0x02};
inline constexpr uint8_t kILI9881CData116[] = {0x01};
inline constexpr uint8_t kILI9881CData117[] = {0x00};
inline constexpr uint8_t kILI9881CData118[] = {0x02};
inline constexpr uint8_t kILI9881CData119[] = {0x14};
inline constexpr uint8_t kILI9881CData120[] = {0x15};
inline constexpr uint8_t kILI9881CData121[] = {0x02};
inline constexpr uint8_t kILI9881CData122[] = {0x02};
inline constexpr uint8_t kILI9881CData123[] = {0x02};
inline constexpr uint8_t kILI9881CData124[] = {0x02};
inline constexpr uint8_t kILI9881CData125[] = {0x02};
inline constexpr uint8_t kILI9881CData126[] = {0x02};
inline constexpr uint8_t kILI9881CData127[] = {0x02};
inline constexpr uint8_t kILI9881CData128[] = {0x98, 0x81, 0x04};
inline constexpr uint8_t kILI9881CData129[] = {0x01};
inline constexpr uint8_t kILI9881CData130[] = {0x00};
inline constexpr uint8_t kILI9881CData131[] = {0x15};
inline constexpr uint8_t kILI9881CData132[] = {0x2A};
inline constexpr uint8_t kILI9881CData133[] = {0x33};
inline constexpr uint8_t kILI9881CData134[] = {0x94};
inline constexpr uint8_t kILI9881CData135[] = {0x14};
inline constexpr uint8_t kILI9881CData136[] = {0xBA};
inline constexpr uint8_t kILI9881CData137[] = {0x76};
inline constexpr uint8_t kILI9881CData138[] = {0xD1};
inline constexpr uint8_t kILI9881CData139[] = {0x06};
inline constexpr uint8_t kILI9881CData140[] = {0x98, 0x81, 0x01};
inline constexpr uint8_t kILI9881CData141[] = {0x0A};
inline constexpr uint8_t kILI9881CData142[] = {0x00};
inline constexpr uint8_t kILI9881CData143[] = {0x71};
inline constexpr uint8_t kILI9881CData144[] = {0x8F};
inline constexpr uint8_t kILI9881CData145[] = {0x33};
inline constexpr uint8_t kILI9881CData146[] = {0x96};
inline constexpr uint8_t kILI9881CData147[] = {0x96};
inline constexpr uint8_t kILI9881CData148[] = {0x23};
inline constexpr uint8_t kILI9881CData149[] = {0x08};
inline constexpr uint8_t kILI9881CData150[] = {0x1D};
inline constexpr uint8_t kILI9881CData151[] = {0x2A};
inline constexpr uint8_t kILI9881CData152[] = {0x10};
inline constexpr uint8_t kILI9881CData153[] = {0x15};
inline constexpr uint8_t kILI9881CData154[] = {0x28};
inline constexpr uint8_t kILI9881CData155[] = {0x1C};
inline constexpr uint8_t kILI9881CData156[] = {0x1D};
inline constexpr uint8_t kILI9881CData157[] = {0x7E};
inline constexpr uint8_t kILI9881CData158[] = {0x1D};
inline constexpr uint8_t kILI9881CData159[] = {0x29};
inline constexpr uint8_t kILI9881CData160[] = {0x6B};
inline constexpr uint8_t kILI9881CData161[] = {0x1A};
inline constexpr uint8_t kILI9881CData162[] = {0x18};
inline constexpr uint8_t kILI9881CData163[] = {0x4B};
inline constexpr uint8_t kILI9881CData164[] = {0x20};
inline constexpr uint8_t kILI9881CData165[] = {0x27};
inline constexpr uint8_t kILI9881CData166[] = {0x50};
inline constexpr uint8_t kILI9881CData167[] = {0x64};
inline constexpr uint8_t kILI9881CData168[] = {0x39};
inline constexpr uint8_t kILI9881CData169[] = {0x08};
inline constexpr uint8_t kILI9881CData170[] = {0x1D};
inline constexpr uint8_t kILI9881CData171[] = {0x2A};
inline constexpr uint8_t kILI9881CData172[] = {0x10};
inline constexpr uint8_t kILI9881CData173[] = {0x15};
inline constexpr uint8_t kILI9881CData174[] = {0x28};
inline constexpr uint8_t kILI9881CData175[] = {0x1C};
inline constexpr uint8_t kILI9881CData176[] = {0x1D};
inline constexpr uint8_t kILI9881CData177[] = {0x7E};
inline constexpr uint8_t kILI9881CData178[] = {0x1D};
inline constexpr uint8_t kILI9881CData179[] = {0x29};
inline constexpr uint8_t kILI9881CData180[] = {0x6B};
inline constexpr uint8_t kILI9881CData181[] = {0x1A};
inline constexpr uint8_t kILI9881CData182[] = {0x18};
inline constexpr uint8_t kILI9881CData183[] = {0x4B};
inline constexpr uint8_t kILI9881CData184[] = {0x20};
inline constexpr uint8_t kILI9881CData185[] = {0x27};
inline constexpr uint8_t kILI9881CData186[] = {0x50};
inline constexpr uint8_t kILI9881CData187[] = {0x64};
inline constexpr uint8_t kILI9881CData188[] = {0x39};
inline constexpr uint8_t kILI9881CData189[] = {0x98, 0x81, 0x00};
inline constexpr uint8_t kILI9881CData190[] = {0x77};
inline constexpr uint8_t kILI9881CData191[] = {0x00};
inline constexpr uint8_t kILI9881CData192[] = {0x00};
inline constexpr uint8_t kILI9881CData193[] = {0x00};
inline constexpr uint8_t kILI9881CData194[] = {0x00};
inline constexpr uint8_t kILI9881CData195[] = {0x00};

inline const ili9881c_lcd_init_cmd_t waveshare_p4_ili9881c_init_cmds[] = {
    {0xFF, kILI9881CData0, 3, 0},
    {0x01, kILI9881CData1, 1, 0},
    {0x02, kILI9881CData2, 1, 0},
    {0x03, kILI9881CData3, 1, 0},
    {0x04, kILI9881CData4, 1, 0},
    {0x05, kILI9881CData5, 1, 0},
    {0x06, kILI9881CData6, 1, 0},
    {0x07, kILI9881CData7, 1, 0},
    {0x08, kILI9881CData8, 1, 0},
    {0x09, kILI9881CData9, 1, 0},
    {0x0A, kILI9881CData10, 1, 0},
    {0x0B, kILI9881CData11, 1, 0},
    {0x0C, kILI9881CData12, 1, 0},
    {0x0D, kILI9881CData13, 1, 0},
    {0x0E, kILI9881CData14, 1, 0},
    {0x0F, kILI9881CData15, 1, 0},
    {0x10, kILI9881CData16, 1, 0},
    {0x11, kILI9881CData17, 1, 0},
    {0x12, kILI9881CData18, 1, 0},
    {0x13, kILI9881CData19, 1, 0},
    {0x14, kILI9881CData20, 1, 0},
    {0x15, kILI9881CData21, 1, 0},
    {0x16, kILI9881CData22, 1, 0},
    {0x17, kILI9881CData23, 1, 0},
    {0x18, kILI9881CData24, 1, 0},
    {0x19, kILI9881CData25, 1, 0},
    {0x1A, kILI9881CData26, 1, 0},
    {0x1B, kILI9881CData27, 1, 0},
    {0x1C, kILI9881CData28, 1, 0},
    {0x1D, kILI9881CData29, 1, 0},
    {0x1E, kILI9881CData30, 1, 0},
    {0x1F, kILI9881CData31, 1, 0},
    {0x20, kILI9881CData32, 1, 0},
    {0x21, kILI9881CData33, 1, 0},
    {0x22, kILI9881CData34, 1, 0},
    {0x23, kILI9881CData35, 1, 0},
    {0x24, kILI9881CData36, 1, 0},
    {0x25, kILI9881CData37, 1, 0},
    {0x26, kILI9881CData38, 1, 0},
    {0x27, kILI9881CData39, 1, 0},
    {0x28, kILI9881CData40, 1, 0},
    {0x29, kILI9881CData41, 1, 0},
    {0x2A, kILI9881CData42, 1, 0},
    {0x2B, kILI9881CData43, 1, 0},
    {0x2C, kILI9881CData44, 1, 0},
    {0x2D, kILI9881CData45, 1, 0},
    {0x2E, kILI9881CData46, 1, 0},
    {0x2F, kILI9881CData47, 1, 0},
    {0x30, kILI9881CData48, 1, 0},
    {0x31, kILI9881CData49, 1, 0},
    {0x32, kILI9881CData50, 1, 0},
    {0x33, kILI9881CData51, 1, 0},
    {0x34, kILI9881CData52, 1, 0},
    {0x35, kILI9881CData53, 1, 0},
    {0x36, kILI9881CData54, 1, 0},
    {0x37, kILI9881CData55, 1, 0},
    {0x38, kILI9881CData56, 1, 0},
    {0x39, kILI9881CData57, 1, 0},
    {0x3A, kILI9881CData58, 1, 0},
    {0x3B, kILI9881CData59, 1, 0},
    {0x3C, kILI9881CData60, 1, 0},
    {0x3D, kILI9881CData61, 1, 0},
    {0x3E, kILI9881CData62, 1, 0},
    {0x3F, kILI9881CData63, 1, 0},
    {0x40, kILI9881CData64, 1, 0},
    {0x41, kILI9881CData65, 1, 0},
    {0x42, kILI9881CData66, 1, 0},
    {0x43, kILI9881CData67, 1, 0},
    {0x44, kILI9881CData68, 1, 0},
    {0x50, kILI9881CData69, 1, 0},
    {0x51, kILI9881CData70, 1, 0},
    {0x52, kILI9881CData71, 1, 0},
    {0x53, kILI9881CData72, 1, 0},
    {0x54, kILI9881CData73, 1, 0},
    {0x55, kILI9881CData74, 1, 0},
    {0x56, kILI9881CData75, 1, 0},
    {0x57, kILI9881CData76, 1, 0},
    {0x58, kILI9881CData77, 1, 0},
    {0x59, kILI9881CData78, 1, 0},
    {0x5A, kILI9881CData79, 1, 0},
    {0x5B, kILI9881CData80, 1, 0},
    {0x5C, kILI9881CData81, 1, 0},
    {0x5D, kILI9881CData82, 1, 0},
    {0x5E, kILI9881CData83, 1, 0},
    {0x5F, kILI9881CData84, 1, 0},
    {0x60, kILI9881CData85, 1, 0},
    {0x61, kILI9881CData86, 1, 0},
    {0x62, kILI9881CData87, 1, 0},
    {0x63, kILI9881CData88, 1, 0},
    {0x64, kILI9881CData89, 1, 0},
    {0x65, kILI9881CData90, 1, 0},
    {0x66, kILI9881CData91, 1, 0},
    {0x67, kILI9881CData92, 1, 0},
    {0x68, kILI9881CData93, 1, 0},
    {0x69, kILI9881CData94, 1, 0},
    {0x6A, kILI9881CData95, 1, 0},
    {0x6B, kILI9881CData96, 1, 0},
    {0x6C, kILI9881CData97, 1, 0},
    {0x6D, kILI9881CData98, 1, 0},
    {0x6E, kILI9881CData99, 1, 0},
    {0x6F, kILI9881CData100, 1, 0},
    {0x70, kILI9881CData101, 1, 0},
    {0x71, kILI9881CData102, 1, 0},
    {0x72, kILI9881CData103, 1, 0},
    {0x73, kILI9881CData104, 1, 0},
    {0x74, kILI9881CData105, 1, 0},
    {0x75, kILI9881CData106, 1, 0},
    {0x76, kILI9881CData107, 1, 0},
    {0x77, kILI9881CData108, 1, 0},
    {0x78, kILI9881CData109, 1, 0},
    {0x79, kILI9881CData110, 1, 0},
    {0x7A, kILI9881CData111, 1, 0},
    {0x7B, kILI9881CData112, 1, 0},
    {0x7C, kILI9881CData113, 1, 0},
    {0x7D, kILI9881CData114, 1, 0},
    {0x7E, kILI9881CData115, 1, 0},
    {0x7F, kILI9881CData116, 1, 0},
    {0x80, kILI9881CData117, 1, 0},
    {0x81, kILI9881CData118, 1, 0},
    {0x82, kILI9881CData119, 1, 0},
    {0x83, kILI9881CData120, 1, 0},
    {0x84, kILI9881CData121, 1, 0},
    {0x85, kILI9881CData122, 1, 0},
    {0x86, kILI9881CData123, 1, 0},
    {0x87, kILI9881CData124, 1, 0},
    {0x88, kILI9881CData125, 1, 0},
    {0x89, kILI9881CData126, 1, 0},
    {0x8A, kILI9881CData127, 1, 0},
    {0xFF, kILI9881CData128, 3, 0},
    {0x38, kILI9881CData129, 1, 0},
    {0x39, kILI9881CData130, 1, 0},
    {0x6C, kILI9881CData131, 1, 0},
    {0x6E, kILI9881CData132, 1, 0},
    {0x6F, kILI9881CData133, 1, 0},
    {0x3A, kILI9881CData134, 1, 0},
    {0x8D, kILI9881CData135, 1, 0},
    {0x87, kILI9881CData136, 1, 0},
    {0x26, kILI9881CData137, 1, 0},
    {0xB2, kILI9881CData138, 1, 0},
    {0xB5, kILI9881CData139, 1, 0},
    {0xFF, kILI9881CData140, 3, 0},
    {0x22, kILI9881CData141, 1, 0},
    {0x31, kILI9881CData142, 1, 0},
    {0x53, kILI9881CData143, 1, 0},
    {0x55, kILI9881CData144, 1, 0},
    {0x40, kILI9881CData145, 1, 0},
    {0x50, kILI9881CData146, 1, 0},
    {0x51, kILI9881CData147, 1, 0},
    {0x60, kILI9881CData148, 1, 0},
    {0xA0, kILI9881CData149, 1, 0},
    {0xA1, kILI9881CData150, 1, 0},
    {0xA2, kILI9881CData151, 1, 0},
    {0xA3, kILI9881CData152, 1, 0},
    {0xA4, kILI9881CData153, 1, 0},
    {0xA5, kILI9881CData154, 1, 0},
    {0xA6, kILI9881CData155, 1, 0},
    {0xA7, kILI9881CData156, 1, 0},
    {0xA8, kILI9881CData157, 1, 0},
    {0xA9, kILI9881CData158, 1, 0},
    {0xAA, kILI9881CData159, 1, 0},
    {0xAB, kILI9881CData160, 1, 0},
    {0xAC, kILI9881CData161, 1, 0},
    {0xAD, kILI9881CData162, 1, 0},
    {0xAE, kILI9881CData163, 1, 0},
    {0xAF, kILI9881CData164, 1, 0},
    {0xB0, kILI9881CData165, 1, 0},
    {0xB1, kILI9881CData166, 1, 0},
    {0xB2, kILI9881CData167, 1, 0},
    {0xB3, kILI9881CData168, 1, 0},
    {0xC0, kILI9881CData169, 1, 0},
    {0xC1, kILI9881CData170, 1, 0},
    {0xC2, kILI9881CData171, 1, 0},
    {0xC3, kILI9881CData172, 1, 0},
    {0xC4, kILI9881CData173, 1, 0},
    {0xC5, kILI9881CData174, 1, 0},
    {0xC6, kILI9881CData175, 1, 0},
    {0xC7, kILI9881CData176, 1, 0},
    {0xC8, kILI9881CData177, 1, 0},
    {0xC9, kILI9881CData178, 1, 0},
    {0xCA, kILI9881CData179, 1, 0},
    {0xCB, kILI9881CData180, 1, 0},
    {0xCC, kILI9881CData181, 1, 0},
    {0xCD, kILI9881CData182, 1, 0},
    {0xCE, kILI9881CData183, 1, 0},
    {0xCF, kILI9881CData184, 1, 0},
    {0xD0, kILI9881CData185, 1, 0},
    {0xD1, kILI9881CData186, 1, 0},
    {0xD2, kILI9881CData187, 1, 0},
    {0xD3, kILI9881CData188, 1, 0},
    {0xFF, kILI9881CData189, 3, 0},
    {0x3A, kILI9881CData190, 1, 0},
    {0x36, kILI9881CData191, 1, 0},
    {0x35, kILI9881CData192, 1, 0},
    {0x35, kILI9881CData193, 1, 0},
    {0x11, kILI9881CData194, 0, 150},
    {0x29, kILI9881CData195, 0, 20},
};
