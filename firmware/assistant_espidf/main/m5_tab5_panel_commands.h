#pragma once
#include <cstdint>
#include "esp_lcd_ili9881c.h"
#include "third_party/xiaozhi_tab5/esp_lcd_st7123.h"

// Fixed 78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e.
// MIT license: third_party/xiaozhi_tab5/LICENSE-MIT
// ST7123 A3/E8 lengths are limited to their actual39/14byte payloads.

inline constexpr uint8_t kTab5_ili_0[] = {0x98, 0x81, 0x01};
inline constexpr uint8_t kTab5_ili_1[] = {0x03};
inline constexpr uint8_t kTab5_ili_2[] = {0x98, 0x81, 0x03};
inline constexpr uint8_t kTab5_ili_3[] = {0x00};
inline constexpr uint8_t kTab5_ili_4[] = {0x00};
inline constexpr uint8_t kTab5_ili_5[] = {0x73};
inline constexpr uint8_t kTab5_ili_6[] = {0x00};
inline constexpr uint8_t kTab5_ili_7[] = {0x00};
inline constexpr uint8_t kTab5_ili_8[] = {0x08};
inline constexpr uint8_t kTab5_ili_9[] = {0x00};
inline constexpr uint8_t kTab5_ili_10[] = {0x00};
inline constexpr uint8_t kTab5_ili_11[] = {0x1B};
inline constexpr uint8_t kTab5_ili_12[] = {0x01};
inline constexpr uint8_t kTab5_ili_13[] = {0x01};
inline constexpr uint8_t kTab5_ili_14[] = {0x0D};
inline constexpr uint8_t kTab5_ili_15[] = {0x01};
inline constexpr uint8_t kTab5_ili_16[] = {0x01};
inline constexpr uint8_t kTab5_ili_17[] = {0x26};
inline constexpr uint8_t kTab5_ili_18[] = {0x26};
inline constexpr uint8_t kTab5_ili_19[] = {0x00};
inline constexpr uint8_t kTab5_ili_20[] = {0x00};
inline constexpr uint8_t kTab5_ili_21[] = {0x02};
inline constexpr uint8_t kTab5_ili_22[] = {0x00};
inline constexpr uint8_t kTab5_ili_23[] = {0x00};
inline constexpr uint8_t kTab5_ili_24[] = {0x00};
inline constexpr uint8_t kTab5_ili_25[] = {0x00};
inline constexpr uint8_t kTab5_ili_26[] = {0x00};
inline constexpr uint8_t kTab5_ili_27[] = {0x00};
inline constexpr uint8_t kTab5_ili_28[] = {0x00};
inline constexpr uint8_t kTab5_ili_29[] = {0x00};
inline constexpr uint8_t kTab5_ili_30[] = {0x00};
inline constexpr uint8_t kTab5_ili_31[] = {0x00};
inline constexpr uint8_t kTab5_ili_32[] = {0x40};
inline constexpr uint8_t kTab5_ili_33[] = {0x00};
inline constexpr uint8_t kTab5_ili_34[] = {0x06};
inline constexpr uint8_t kTab5_ili_35[] = {0x01};
inline constexpr uint8_t kTab5_ili_36[] = {0x00};
inline constexpr uint8_t kTab5_ili_37[] = {0x00};
inline constexpr uint8_t kTab5_ili_38[] = {0x00};
inline constexpr uint8_t kTab5_ili_39[] = {0x00};
inline constexpr uint8_t kTab5_ili_40[] = {0x00};
inline constexpr uint8_t kTab5_ili_41[] = {0x00};
inline constexpr uint8_t kTab5_ili_42[] = {0x33};
inline constexpr uint8_t kTab5_ili_43[] = {0x03};
inline constexpr uint8_t kTab5_ili_44[] = {0x00};
inline constexpr uint8_t kTab5_ili_45[] = {0x00};
inline constexpr uint8_t kTab5_ili_46[] = {0x00};
inline constexpr uint8_t kTab5_ili_47[] = {0x00};
inline constexpr uint8_t kTab5_ili_48[] = {0x00};
inline constexpr uint8_t kTab5_ili_49[] = {0x00};
inline constexpr uint8_t kTab5_ili_50[] = {0x00};
inline constexpr uint8_t kTab5_ili_51[] = {0x00};
inline constexpr uint8_t kTab5_ili_52[] = {0x00};
inline constexpr uint8_t kTab5_ili_53[] = {0x00};
inline constexpr uint8_t kTab5_ili_54[] = {0x00};
inline constexpr uint8_t kTab5_ili_55[] = {0x00};
inline constexpr uint8_t kTab5_ili_56[] = {0x00};
inline constexpr uint8_t kTab5_ili_57[] = {0x00};
inline constexpr uint8_t kTab5_ili_58[] = {0x00};
inline constexpr uint8_t kTab5_ili_59[] = {0x00};
inline constexpr uint8_t kTab5_ili_60[] = {0x00};
inline constexpr uint8_t kTab5_ili_61[] = {0x00};
inline constexpr uint8_t kTab5_ili_62[] = {0x00};
inline constexpr uint8_t kTab5_ili_63[] = {0x00};
inline constexpr uint8_t kTab5_ili_64[] = {0x00};
inline constexpr uint8_t kTab5_ili_65[] = {0x00};
inline constexpr uint8_t kTab5_ili_66[] = {0x00};
inline constexpr uint8_t kTab5_ili_67[] = {0x00};
inline constexpr uint8_t kTab5_ili_68[] = {0x00};
inline constexpr uint8_t kTab5_ili_69[] = {0x00};
inline constexpr uint8_t kTab5_ili_70[] = {0x00};
inline constexpr uint8_t kTab5_ili_71[] = {0x01};
inline constexpr uint8_t kTab5_ili_72[] = {0x23};
inline constexpr uint8_t kTab5_ili_73[] = {0x45};
inline constexpr uint8_t kTab5_ili_74[] = {0x67};
inline constexpr uint8_t kTab5_ili_75[] = {0x89};
inline constexpr uint8_t kTab5_ili_76[] = {0xab};
inline constexpr uint8_t kTab5_ili_77[] = {0x01};
inline constexpr uint8_t kTab5_ili_78[] = {0x23};
inline constexpr uint8_t kTab5_ili_79[] = {0x45};
inline constexpr uint8_t kTab5_ili_80[] = {0x67};
inline constexpr uint8_t kTab5_ili_81[] = {0x89};
inline constexpr uint8_t kTab5_ili_82[] = {0xab};
inline constexpr uint8_t kTab5_ili_83[] = {0xcd};
inline constexpr uint8_t kTab5_ili_84[] = {0xef};
inline constexpr uint8_t kTab5_ili_85[] = {0x11};
inline constexpr uint8_t kTab5_ili_86[] = {0x02};
inline constexpr uint8_t kTab5_ili_87[] = {0x00};
inline constexpr uint8_t kTab5_ili_88[] = {0x07};
inline constexpr uint8_t kTab5_ili_89[] = {0x06};
inline constexpr uint8_t kTab5_ili_90[] = {0x0E};
inline constexpr uint8_t kTab5_ili_91[] = {0x0F};
inline constexpr uint8_t kTab5_ili_92[] = {0x0C};
inline constexpr uint8_t kTab5_ili_93[] = {0x0D};
inline constexpr uint8_t kTab5_ili_94[] = {0x02};
inline constexpr uint8_t kTab5_ili_95[] = {0x02};
inline constexpr uint8_t kTab5_ili_96[] = {0x02};
inline constexpr uint8_t kTab5_ili_97[] = {0x02};
inline constexpr uint8_t kTab5_ili_98[] = {0x02};
inline constexpr uint8_t kTab5_ili_99[] = {0x02};
inline constexpr uint8_t kTab5_ili_100[] = {0x02};
inline constexpr uint8_t kTab5_ili_101[] = {0x02};
inline constexpr uint8_t kTab5_ili_102[] = {0x02};
inline constexpr uint8_t kTab5_ili_103[] = {0x02};
inline constexpr uint8_t kTab5_ili_104[] = {0x02};
inline constexpr uint8_t kTab5_ili_105[] = {0x02};
inline constexpr uint8_t kTab5_ili_106[] = {0x05};
inline constexpr uint8_t kTab5_ili_107[] = {0x01};
inline constexpr uint8_t kTab5_ili_108[] = {0x02};
inline constexpr uint8_t kTab5_ili_109[] = {0x00};
inline constexpr uint8_t kTab5_ili_110[] = {0x07};
inline constexpr uint8_t kTab5_ili_111[] = {0x06};
inline constexpr uint8_t kTab5_ili_112[] = {0x0E};
inline constexpr uint8_t kTab5_ili_113[] = {0x0F};
inline constexpr uint8_t kTab5_ili_114[] = {0x0C};
inline constexpr uint8_t kTab5_ili_115[] = {0x0D};
inline constexpr uint8_t kTab5_ili_116[] = {0x02};
inline constexpr uint8_t kTab5_ili_117[] = {0x02};
inline constexpr uint8_t kTab5_ili_118[] = {0x02};
inline constexpr uint8_t kTab5_ili_119[] = {0x02};
inline constexpr uint8_t kTab5_ili_120[] = {0x02};
inline constexpr uint8_t kTab5_ili_121[] = {0x02};
inline constexpr uint8_t kTab5_ili_122[] = {0x02};
inline constexpr uint8_t kTab5_ili_123[] = {0x02};
inline constexpr uint8_t kTab5_ili_124[] = {0x02};
inline constexpr uint8_t kTab5_ili_125[] = {0x02};
inline constexpr uint8_t kTab5_ili_126[] = {0x02};
inline constexpr uint8_t kTab5_ili_127[] = {0x02};
inline constexpr uint8_t kTab5_ili_128[] = {0x05};
inline constexpr uint8_t kTab5_ili_129[] = {0x01};
inline constexpr uint8_t kTab5_ili_130[] = {0x98, 0x81, 0x04};
inline constexpr uint8_t kTab5_ili_131[] = {0x01};
inline constexpr uint8_t kTab5_ili_132[] = {0x00};
inline constexpr uint8_t kTab5_ili_133[] = {0x15};
inline constexpr uint8_t kTab5_ili_134[] = {0x1A};
inline constexpr uint8_t kTab5_ili_135[] = {0x25};
inline constexpr uint8_t kTab5_ili_136[] = {0xA4};
inline constexpr uint8_t kTab5_ili_137[] = {0x20};
inline constexpr uint8_t kTab5_ili_138[] = {0xBA};
inline constexpr uint8_t kTab5_ili_139[] = {0x98};
inline constexpr uint8_t kTab5_ili_140[] = {0x98, 0x81, 0x01};
inline constexpr uint8_t kTab5_ili_141[] = {0x0A};
inline constexpr uint8_t kTab5_ili_142[] = {0x00};
inline constexpr uint8_t kTab5_ili_143[] = {0x6B};
inline constexpr uint8_t kTab5_ili_144[] = {0x66};
inline constexpr uint8_t kTab5_ili_145[] = {0x73};
inline constexpr uint8_t kTab5_ili_146[] = {0x8B};
inline constexpr uint8_t kTab5_ili_147[] = {0x1B};
inline constexpr uint8_t kTab5_ili_148[] = {0x01};
inline constexpr uint8_t kTab5_ili_149[] = {0x0C};
inline constexpr uint8_t kTab5_ili_150[] = {0x00};
inline constexpr uint8_t kTab5_ili_151[] = {0x00};
inline constexpr uint8_t kTab5_ili_152[] = {0x15};
inline constexpr uint8_t kTab5_ili_153[] = {0x1F};
inline constexpr uint8_t kTab5_ili_154[] = {0x13};
inline constexpr uint8_t kTab5_ili_155[] = {0x11};
inline constexpr uint8_t kTab5_ili_156[] = {0x21};
inline constexpr uint8_t kTab5_ili_157[] = {0x17};
inline constexpr uint8_t kTab5_ili_158[] = {0x1B};
inline constexpr uint8_t kTab5_ili_159[] = {0x6B};
inline constexpr uint8_t kTab5_ili_160[] = {0x1E};
inline constexpr uint8_t kTab5_ili_161[] = {0x2B};
inline constexpr uint8_t kTab5_ili_162[] = {0x5D};
inline constexpr uint8_t kTab5_ili_163[] = {0x19};
inline constexpr uint8_t kTab5_ili_164[] = {0x14};
inline constexpr uint8_t kTab5_ili_165[] = {0x4B};
inline constexpr uint8_t kTab5_ili_166[] = {0x1D};
inline constexpr uint8_t kTab5_ili_167[] = {0x27};
inline constexpr uint8_t kTab5_ili_168[] = {0x49};
inline constexpr uint8_t kTab5_ili_169[] = {0x5D};
inline constexpr uint8_t kTab5_ili_170[] = {0x39};
inline constexpr uint8_t kTab5_ili_171[] = {0x00};
inline constexpr uint8_t kTab5_ili_172[] = {0x01};
inline constexpr uint8_t kTab5_ili_173[] = {0x0C};
inline constexpr uint8_t kTab5_ili_174[] = {0x11};
inline constexpr uint8_t kTab5_ili_175[] = {0x15};
inline constexpr uint8_t kTab5_ili_176[] = {0x28};
inline constexpr uint8_t kTab5_ili_177[] = {0x1B};
inline constexpr uint8_t kTab5_ili_178[] = {0x1C};
inline constexpr uint8_t kTab5_ili_179[] = {0x62};
inline constexpr uint8_t kTab5_ili_180[] = {0x1C};
inline constexpr uint8_t kTab5_ili_181[] = {0x29};
inline constexpr uint8_t kTab5_ili_182[] = {0x60};
inline constexpr uint8_t kTab5_ili_183[] = {0x16};
inline constexpr uint8_t kTab5_ili_184[] = {0x17};
inline constexpr uint8_t kTab5_ili_185[] = {0x4A};
inline constexpr uint8_t kTab5_ili_186[] = {0x23};
inline constexpr uint8_t kTab5_ili_187[] = {0x24};
inline constexpr uint8_t kTab5_ili_188[] = {0x4F};
inline constexpr uint8_t kTab5_ili_189[] = {0x5F};
inline constexpr uint8_t kTab5_ili_190[] = {0x39};
inline constexpr uint8_t kTab5_ili_191[] = {0x98, 0x81, 0x00};
inline constexpr uint8_t kTab5_ili_192[] = {0x00};
inline constexpr uint8_t kTab5_ili_193[] = {0x00};
inline constexpr uint8_t kTab5_ili_194[] = {0x00};
inline constexpr ili9881c_lcd_init_cmd_t tab5_ili_commands[] = {
    {0xFF, kTab5_ili_0, 3, 0},
    {0xB7, kTab5_ili_1, 1, 0},
    {0xFF, kTab5_ili_2, 3, 0},
    {0x01, kTab5_ili_3, 1, 0},
    {0x02, kTab5_ili_4, 1, 0},
    {0x03, kTab5_ili_5, 1, 0},
    {0x04, kTab5_ili_6, 1, 0},
    {0x05, kTab5_ili_7, 1, 0},
    {0x06, kTab5_ili_8, 1, 0},
    {0x07, kTab5_ili_9, 1, 0},
    {0x08, kTab5_ili_10, 1, 0},
    {0x09, kTab5_ili_11, 1, 0},
    {0x0A, kTab5_ili_12, 1, 0},
    {0x0B, kTab5_ili_13, 1, 0},
    {0x0C, kTab5_ili_14, 1, 0},
    {0x0D, kTab5_ili_15, 1, 0},
    {0x0E, kTab5_ili_16, 1, 0},
    {0x0F, kTab5_ili_17, 1, 0},
    {0x10, kTab5_ili_18, 1, 0},
    {0x11, kTab5_ili_19, 1, 0},
    {0x12, kTab5_ili_20, 1, 0},
    {0x13, kTab5_ili_21, 1, 0},
    {0x14, kTab5_ili_22, 1, 0},
    {0x15, kTab5_ili_23, 1, 0},
    {0x16, kTab5_ili_24, 1, 0},
    {0x17, kTab5_ili_25, 1, 0},
    {0x18, kTab5_ili_26, 1, 0},
    {0x19, kTab5_ili_27, 1, 0},
    {0x1A, kTab5_ili_28, 1, 0},
    {0x1B, kTab5_ili_29, 1, 0},
    {0x1C, kTab5_ili_30, 1, 0},
    {0x1D, kTab5_ili_31, 1, 0},
    {0x1E, kTab5_ili_32, 1, 0},
    {0x1F, kTab5_ili_33, 1, 0},
    {0x20, kTab5_ili_34, 1, 0},
    {0x21, kTab5_ili_35, 1, 0},
    {0x22, kTab5_ili_36, 1, 0},
    {0x23, kTab5_ili_37, 1, 0},
    {0x24, kTab5_ili_38, 1, 0},
    {0x25, kTab5_ili_39, 1, 0},
    {0x26, kTab5_ili_40, 1, 0},
    {0x27, kTab5_ili_41, 1, 0},
    {0x28, kTab5_ili_42, 1, 0},
    {0x29, kTab5_ili_43, 1, 0},
    {0x2A, kTab5_ili_44, 1, 0},
    {0x2B, kTab5_ili_45, 1, 0},
    {0x2C, kTab5_ili_46, 1, 0},
    {0x2D, kTab5_ili_47, 1, 0},
    {0x2E, kTab5_ili_48, 1, 0},
    {0x2F, kTab5_ili_49, 1, 0},
    {0x30, kTab5_ili_50, 1, 0},
    {0x31, kTab5_ili_51, 1, 0},
    {0x32, kTab5_ili_52, 1, 0},
    {0x33, kTab5_ili_53, 1, 0},
    {0x34, kTab5_ili_54, 1, 0},
    {0x35, kTab5_ili_55, 1, 0},
    {0x36, kTab5_ili_56, 1, 0},
    {0x37, kTab5_ili_57, 1, 0},
    {0x38, kTab5_ili_58, 1, 0},
    {0x39, kTab5_ili_59, 1, 0},
    {0x3A, kTab5_ili_60, 1, 0},
    {0x3B, kTab5_ili_61, 1, 0},
    {0x3C, kTab5_ili_62, 1, 0},
    {0x3D, kTab5_ili_63, 1, 0},
    {0x3E, kTab5_ili_64, 1, 0},
    {0x3F, kTab5_ili_65, 1, 0},
    {0x40, kTab5_ili_66, 1, 0},
    {0x41, kTab5_ili_67, 1, 0},
    {0x42, kTab5_ili_68, 1, 0},
    {0x43, kTab5_ili_69, 1, 0},
    {0x44, kTab5_ili_70, 1, 0},
    {0x50, kTab5_ili_71, 1, 0},
    {0x51, kTab5_ili_72, 1, 0},
    {0x52, kTab5_ili_73, 1, 0},
    {0x53, kTab5_ili_74, 1, 0},
    {0x54, kTab5_ili_75, 1, 0},
    {0x55, kTab5_ili_76, 1, 0},
    {0x56, kTab5_ili_77, 1, 0},
    {0x57, kTab5_ili_78, 1, 0},
    {0x58, kTab5_ili_79, 1, 0},
    {0x59, kTab5_ili_80, 1, 0},
    {0x5A, kTab5_ili_81, 1, 0},
    {0x5B, kTab5_ili_82, 1, 0},
    {0x5C, kTab5_ili_83, 1, 0},
    {0x5D, kTab5_ili_84, 1, 0},
    {0x5E, kTab5_ili_85, 1, 0},
    {0x5F, kTab5_ili_86, 1, 0},
    {0x60, kTab5_ili_87, 1, 0},
    {0x61, kTab5_ili_88, 1, 0},
    {0x62, kTab5_ili_89, 1, 0},
    {0x63, kTab5_ili_90, 1, 0},
    {0x64, kTab5_ili_91, 1, 0},
    {0x65, kTab5_ili_92, 1, 0},
    {0x66, kTab5_ili_93, 1, 0},
    {0x67, kTab5_ili_94, 1, 0},
    {0x68, kTab5_ili_95, 1, 0},
    {0x69, kTab5_ili_96, 1, 0},
    {0x6A, kTab5_ili_97, 1, 0},
    {0x6B, kTab5_ili_98, 1, 0},
    {0x6C, kTab5_ili_99, 1, 0},
    {0x6D, kTab5_ili_100, 1, 0},
    {0x6E, kTab5_ili_101, 1, 0},
    {0x6F, kTab5_ili_102, 1, 0},
    {0x70, kTab5_ili_103, 1, 0},
    {0x71, kTab5_ili_104, 1, 0},
    {0x72, kTab5_ili_105, 1, 0},
    {0x73, kTab5_ili_106, 1, 0},
    {0x74, kTab5_ili_107, 1, 0},
    {0x75, kTab5_ili_108, 1, 0},
    {0x76, kTab5_ili_109, 1, 0},
    {0x77, kTab5_ili_110, 1, 0},
    {0x78, kTab5_ili_111, 1, 0},
    {0x79, kTab5_ili_112, 1, 0},
    {0x7A, kTab5_ili_113, 1, 0},
    {0x7B, kTab5_ili_114, 1, 0},
    {0x7C, kTab5_ili_115, 1, 0},
    {0x7D, kTab5_ili_116, 1, 0},
    {0x7E, kTab5_ili_117, 1, 0},
    {0x7F, kTab5_ili_118, 1, 0},
    {0x80, kTab5_ili_119, 1, 0},
    {0x81, kTab5_ili_120, 1, 0},
    {0x82, kTab5_ili_121, 1, 0},
    {0x83, kTab5_ili_122, 1, 0},
    {0x84, kTab5_ili_123, 1, 0},
    {0x85, kTab5_ili_124, 1, 0},
    {0x86, kTab5_ili_125, 1, 0},
    {0x87, kTab5_ili_126, 1, 0},
    {0x88, kTab5_ili_127, 1, 0},
    {0x89, kTab5_ili_128, 1, 0},
    {0x8A, kTab5_ili_129, 1, 0},
    {0xFF, kTab5_ili_130, 3, 0},
    {0x38, kTab5_ili_131, 1, 0},
    {0x39, kTab5_ili_132, 1, 0},
    {0x6C, kTab5_ili_133, 1, 0},
    {0x6E, kTab5_ili_134, 1, 0},
    {0x6F, kTab5_ili_135, 1, 0},
    {0x3A, kTab5_ili_136, 1, 0},
    {0x8D, kTab5_ili_137, 1, 0},
    {0x87, kTab5_ili_138, 1, 0},
    {0x3B, kTab5_ili_139, 1, 0},
    {0xFF, kTab5_ili_140, 3, 0},
    {0x22, kTab5_ili_141, 1, 0},
    {0x31, kTab5_ili_142, 1, 0},
    {0x50, kTab5_ili_143, 1, 0},
    {0x51, kTab5_ili_144, 1, 0},
    {0x53, kTab5_ili_145, 1, 0},
    {0x55, kTab5_ili_146, 1, 0},
    {0x60, kTab5_ili_147, 1, 0},
    {0x61, kTab5_ili_148, 1, 0},
    {0x62, kTab5_ili_149, 1, 0},
    {0x63, kTab5_ili_150, 1, 0},
    {0xA0, kTab5_ili_151, 1, 0},
    {0xA1, kTab5_ili_152, 1, 0},
    {0xA2, kTab5_ili_153, 1, 0},
    {0xA3, kTab5_ili_154, 1, 0},
    {0xA4, kTab5_ili_155, 1, 0},
    {0xA5, kTab5_ili_156, 1, 0},
    {0xA6, kTab5_ili_157, 1, 0},
    {0xA7, kTab5_ili_158, 1, 0},
    {0xA8, kTab5_ili_159, 1, 0},
    {0xA9, kTab5_ili_160, 1, 0},
    {0xAA, kTab5_ili_161, 1, 0},
    {0xAB, kTab5_ili_162, 1, 0},
    {0xAC, kTab5_ili_163, 1, 0},
    {0xAD, kTab5_ili_164, 1, 0},
    {0xAE, kTab5_ili_165, 1, 0},
    {0xAF, kTab5_ili_166, 1, 0},
    {0xB0, kTab5_ili_167, 1, 0},
    {0xB1, kTab5_ili_168, 1, 0},
    {0xB2, kTab5_ili_169, 1, 0},
    {0xB3, kTab5_ili_170, 1, 0},
    {0xC0, kTab5_ili_171, 1, 0},
    {0xC1, kTab5_ili_172, 1, 0},
    {0xC2, kTab5_ili_173, 1, 0},
    {0xC3, kTab5_ili_174, 1, 0},
    {0xC4, kTab5_ili_175, 1, 0},
    {0xC5, kTab5_ili_176, 1, 0},
    {0xC6, kTab5_ili_177, 1, 0},
    {0xC7, kTab5_ili_178, 1, 0},
    {0xC8, kTab5_ili_179, 1, 0},
    {0xC9, kTab5_ili_180, 1, 0},
    {0xCA, kTab5_ili_181, 1, 0},
    {0xCB, kTab5_ili_182, 1, 0},
    {0xCC, kTab5_ili_183, 1, 0},
    {0xCD, kTab5_ili_184, 1, 0},
    {0xCE, kTab5_ili_185, 1, 0},
    {0xCF, kTab5_ili_186, 1, 0},
    {0xD0, kTab5_ili_187, 1, 0},
    {0xD1, kTab5_ili_188, 1, 0},
    {0xD2, kTab5_ili_189, 1, 0},
    {0xD3, kTab5_ili_190, 1, 0},
    {0xFF, kTab5_ili_191, 3, 0},
    {0x35, kTab5_ili_192, 0, 0},
    {0xFE, kTab5_ili_193, 0, 0},
    {0x29, kTab5_ili_194, 0, 0},
};

inline constexpr uint8_t kTab5_st_0[] = {0x71, 0x23, 0xa2};
inline constexpr uint8_t kTab5_st_1[] = {0x71, 0x23, 0xa3};
inline constexpr uint8_t kTab5_st_2[] = {0x71, 0x23, 0xa4};
inline constexpr uint8_t kTab5_st_3[] = {0x31};
inline constexpr uint8_t kTab5_st_4[] = {0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80};
inline constexpr uint8_t kTab5_st_5[] = {0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09};
inline constexpr uint8_t kTab5_st_6[] = {0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF};
inline constexpr uint8_t kTab5_st_7[] = {0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24,
                       0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00,
                       0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E,
                       0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00};
inline constexpr uint8_t kTab5_st_8[] = {0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44};
inline constexpr uint8_t kTab5_st_9[] = {0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C,
                       0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12,
                       0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01};
inline constexpr uint8_t kTab5_st_10[] = {0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
inline constexpr uint8_t kTab5_st_11[] = {0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00};
inline constexpr uint8_t kTab5_st_12[] = {0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20, 0xFD, 0x20, 0xC0, 0x00};
inline constexpr uint8_t kTab5_st_13[] = {0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E};
inline constexpr uint8_t kTab5_st_14[] = {0x03, 0x04};
inline constexpr uint8_t kTab5_st_15[] = {0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00,
                       0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45,
                       0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77};
inline constexpr uint8_t kTab5_st_16[] = {0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C};
inline constexpr uint8_t kTab5_st_17[] = {0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43};
inline constexpr uint8_t kTab5_st_18[] = {0x00, 0x00, 0x73, 0x73};
inline constexpr uint8_t kTab5_st_19[] = {0xA6, 0xAA};
inline constexpr uint8_t kTab5_st_20[] = {0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03};
inline constexpr uint8_t kTab5_st_21[] = {0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF};
inline constexpr uint8_t kTab5_st_22[] = {0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF};
inline constexpr uint8_t kTab5_st_23[] = {0x00};
inline constexpr uint8_t kTab5_st_24[] = {0x00};
inline constexpr uint8_t kTab5_st_25[] = {0x00};
inline constexpr uint8_t kTab5_st_26[] = {0x00};
inline constexpr st7123_lcd_init_cmd_t tab5_st_commands[] = {
    {0x60, kTab5_st_0, 3, 0},
    {0x60, kTab5_st_1, 3, 0},
    {0x60, kTab5_st_2, 3, 0},
    {0xA4, kTab5_st_3, 1, 0},
    {0xD7, kTab5_st_4, 6, 0},
    {0x90, kTab5_st_5, 7, 0},
    {0xA3, kTab5_st_6, 39, 0},
    {0xA6, kTab5_st_7, 55, 0},
    {0xA7, kTab5_st_8, 60, 0},
    {0xAC, kTab5_st_9, 44, 0},
    {0xAD, kTab5_st_10, 25, 0},
    {0xAE, kTab5_st_11, 7, 0},
    {0xB2, kTab5_st_12, 17, 0},
    {0xE8, kTab5_st_13, 14, 0},
    {0x75, kTab5_st_14, 2, 0},
    {0xE7, kTab5_st_15, 36, 0},
    {0xEA, kTab5_st_16, 8, 0},
    {0xB0, kTab5_st_17, 7, 0},
    {0xB7, kTab5_st_18, 4, 0},
    {0xBF, kTab5_st_19, 2, 0},
    {0xA9, kTab5_st_20, 10, 0},
    {0xC8, kTab5_st_21, 37, 0},
    {0xC9, kTab5_st_22, 37, 0},
    {0x36, kTab5_st_23, 1, 0},
    {0x11, kTab5_st_24, 1, 100},
    {0x29, kTab5_st_25, 1, 0},
    {0x35, kTab5_st_26, 1, 100},
};
