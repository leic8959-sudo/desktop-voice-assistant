#pragma once

#include "board_profile.h"

struct DisplayLayout {
    int content_x, content_width;
    int cloud_x, cloud_y, cloud_width, cloud_height;
    int brand_x, brand_y, status_x, status_y;
    int title_y, detail_y, detail_height, footer_y;
    int badge_y, badge_height, card_title_y, card_title_height;
    int clock_y, clock_height, card_detail_y, route_detail_y, timer_detail_y, detail_bottom;
    int timer_badge_y, timer_clock_y, timer_title_y, timer_small_detail_y;
    int buttons_x, buttons_y, button_width, button_height, button_gap;
    int pairing_heading_y, pairing_bottom;
};

// Explicit layouts keep touch targets and 16px text usable on the short landscape
// screen and keep the round screen's text/buttons inside its visible circle.
#if defined(ASSISTANT_BOARD_P4_JD9365_34C) || defined(ASSISTANT_BOARD_P4_JD9365_4C)
// Keep all card and pairing content in a centered 460px square, inside even
// a 720px circular mask. This does not establish the physical bezel shape.
inline constexpr int JD_CONTENT_X = (board_profile::width - 460) / 2;
inline constexpr int JD_CONTENT_Y = (board_profile::height - 460) / 2;
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    JD_CONTENT_X + 40, 380, JD_CONTENT_X + 70, JD_CONTENT_Y + 40, 320, 180,
    JD_CONTENT_X + 96, JD_CONTENT_Y + 16, JD_CONTENT_X + 350, JD_CONTENT_Y + 23,
    JD_CONTENT_Y + 230, JD_CONTENT_Y + 262, 64, JD_CONTENT_Y + 436,
    JD_CONTENT_Y + 24, 24, JD_CONTENT_Y + 64, 54,
    JD_CONTENT_Y + 138, 32, JD_CONTENT_Y + 182, JD_CONTENT_Y + 246, JD_CONTENT_Y + 220, JD_CONTENT_Y + 338,
    JD_CONTENT_Y + 64, JD_CONTENT_Y + 280, JD_CONTENT_Y + 316, JD_CONTENT_Y + 344,
    JD_CONTENT_X + 64, JD_CONTENT_Y + 374, 156, 48, 20,
    JD_CONTENT_Y + 68, JD_CONTENT_Y + 76,
};
#elif defined(ASSISTANT_BOARD_SENSECAP)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    48, 316, 22, 44, 368, 214, 150, 26, 202, 24,
    258, 286, 62, 396,
    48, 24, 80, 52, 140, 28, 146, 210, 176, 284,
    48, 236, 270, 298,
    80, 320, 116, 44, 20, 56, 50,
};
#elif defined(ASSISTANT_BOARD_MATRIX)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    0, 64, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 14, 50, 0,
    0, 12, 14, 17, 14, 17, 32, 32, 32, 64,
    0, 14, 14, 32,
    0, 64, 0, 0, 0, 0, 0,
};
#elif defined(ASSISTANT_BOARD_WS_RLCD)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 384, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 36, 243, 0,
    0, 20, 28, 24, 0, 20, 64, 64, 64, 226,
    0, 0, 28, 64,
    8, 256, 188, 44, 8, 0, 0,
};
#elif defined(ASSISTANT_BOARD_WS_EPAPER397)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 768, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 56, 403, 0,
    0, 28, 40, 36, 0, 28, 96, 96, 96, 384,
    0, 0, 40, 96,
    16, 424, 376, 56, 16, 0, 0,
};
#elif defined(ASSISTANT_BOARD_ZECTRIX)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 384, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 22, 257, 0,
    0, 12, 16, 17, 0, 12, 36, 36, 36, 235,
    0, 0, 16, 36,
    8, 256, 188, 44, 8, 0, 0,
};
#elif defined(ASSISTANT_BOARD_WS_EPAPER)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 184, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 22, 157, 0,
    0, 12, 16, 17, 0, 12, 36, 36, 36, 135,
    0, 0, 16, 36,
    8, 156, 88, 44, 8, 0, 0,
};
#elif defined(ASSISTANT_BOARD_MAZA)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 280, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 22, 100, 0,
    0, 12, 16, 17, 0, 12, 36, 36, 36, 87,
    0, 0, 16, 36,
    8, 108, 136, 20, 8, 0, 0,
};
#elif defined(ASSISTANT_BOARD_INTERNAL_RAM)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    0, board_profile::width, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 14, board_profile::height - 14, 0,
    0, 12, 14, 17, 14, 17, 34, 34, 34, board_profile::height - 26,
    0, 14, 14, 34,
    0, board_profile::height - 22, (board_profile::width - 4) / 2, 20, 4, 0, 0,
};
#elif defined(ASSISTANT_BOARD_OLED)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    0, 128, 0, 0, 16, 16, 0, 0, 0, 0,
    0, 14, 17, 0,
    0, 12, 13, 17, 13, 17, 30, 30, 30, 47,
    0, 13, 13, 30,
    0, board_profile::height == 64 ? 47 : 32, 62, 17, 4, 0, 0,
};
#elif defined(ASSISTANT_BOARD_STICK_S3)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 119, 0, 30, 135, 84, 8, 8, 122, 14,
    120, 144, 62, 224,
    6, 18, 28, 40, 72, 24, 100, 100, 126, 180,
    6, 58, 88, 114,
    6, 188, 59, 28, 5, 12, 16,
};
#elif defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_MIXGO)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 112, 0, 28, 128, 56, 8, 6, 116, 12,
    88, 110, 18, 144,
    4, 18, 26, 20, 48, 20, 70, 70, 92, 118,
    4, 26, 48, 70,
    4, 124, 56, 28, 8, 8, 8,
};
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    24, 192, 0, 36, 240, 120, 30, 10, 202, 17,
    160, 188, 54, 274,
    12, 20, 38, 44, 86, 24, 90, 126, 118, 222,
    12, 146, 172, 194,
    16, 230, 100, 36, 8, 28, 24,
};
#elif defined(ASSISTANT_BOARD_KEVIN_SPV4)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 208, 0, 32, 240, 116, 16, 8, 218, 15,
    152, 180, 52, 264,
    8, 20, 34, 44, 82, 24, 86, 122, 114, 214,
    8, 142, 168, 190,
    16, 224, 100, 36, 8, 24, 16,
};
#elif defined(ASSISTANT_BOARD_LCD_085) || defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    8, 112, 0, 26, 128, 48, 8, 6, 116, 12,
    78, 98, 20, 124,
    4, 18, 26, 20, 48, 20, 48, 48, 70, 86,
    4, 26, 48, 70,
    4, 92, 56, 28, 8, 8, 8,
};
#elif defined(ASSISTANT_BOARD_LCD_185_FAMILY) || defined(ASSISTANT_BOARD_TAIJI) || defined(ASSISTANT_BOARD_VOCAT)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    68, 224, 36, 36, 288, 188, 102, 30, 178, 18,
    228, 256, 42, 322,
    42, 22, 72, 44, 124, 26, 128, 180, 158, 246,
    42, 208, 236, 258,
    72, 278, 100, 36, 16, 48, 46,
};
#elif defined(ASSISTANT_BOARD_LCD_146)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    68, 276, 42, 44, 328, 214, 128, 34, 204, 20,
    260, 290, 56, 366,
    48, 24, 84, 54, 148, 30, 154, 218, 188, 294,
    48, 242, 272, 296,
    82, 318, 116, 40, 16, 60, 64,
};
#elif defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_BREAD_TFT) || defined(ASSISTANT_BOARD_ATK_BOX2) || (defined(ASSISTANT_BOARD_WS_CAM) && !defined(ASSISTANT_BOARD_WS_CAM_183) && !defined(ASSISTANT_BOARD_WS_CAM_35))
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 208, 0, 36, 240, 148, 16, 10, 218, 17,
    188, 216, 56, 310,
    12, 22, 44, 44, 96, 26, 96, 154, 134, 260,
    12, 154, 184, 212,
    16, 272, 100, 36, 8, 28, 26,
};
#elif defined(ASSISTANT_BOARD_ELECTRON)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    40, 160, 24, 30, 192, 112, 90, 16, 148, 20,
    146, 170, 20, 216,
    26, 20, 52, 20, 76, 20, 92, 92, 116, 150,
    26, 76, 102, 126,
    40, 160, 76, 44, 8, 26, 34,
};
#elif defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    40, 160, 24, 30, 192, 112, 90, 16, 148, 20,
    146, 170, 20, 216,
    26, 20, 52, 20, 76, 20, 92, 92, 116, 150,
    26, 76, 102, 126,
    40, 160, 76, 32, 8, 26, 34,
};
#elif defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_LABPLUS)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    12, 296, 0, 24, 320, 56, 16, 6, 299, 13,
    84, 106, 36, 154,
    6, 18, 28, 20, 50, 22, 74, 74, 96, 120,
    6, 28, 52, 76,
    12, 132, 144, 32, 8, 8, 8,
};
#elif defined(ASSISTANT_BOARD_CHD_LCD35)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    24, 432, 80, 30, 320, 120, 24, 10, 455, 17,
    160, 188, 86, 303,
    10, 22, 38, 44, 88, 26, 122, 154, 136, 246,
    10, 156, 184, 210,
    24, 258, 208, 42, 16, 24, 18,
};
#elif defined(ASSISTANT_BOARD_WS_CAM_183)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 208, 0, 32, 240, 116, 16, 8, 218, 15,
    152, 180, 52, 268,
    8, 20, 34, 44, 82, 24, 86, 122, 114, 218,
    8, 142, 168, 190,
    16, 228, 100, 36, 8, 24, 16,
};
#elif defined(ASSISTANT_BOARD_WS_RGB_C) || defined(ASSISTANT_BOARD_LCD_EV_2) || defined(ASSISTANT_BOARD_S31_KORVO)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    40, 720, 240, 28, 320, 100, 40, 12, 759, 19,
    140, 170, 18, 464,
    16, 24, 56, 54, 120, 32, 176, 260, 228, 384,
    16, 252, 300, 344,
    40, 400, 352, 48, 16, 32, 24,
};
#elif defined(ASSISTANT_BOARD_LCD_EV)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    24, 432, 80, 28, 320, 100, 24, 12, 455, 19,
    140, 170, 18, 464,
    16, 24, 56, 54, 120, 32, 176, 260, 228, 384,
    16, 252, 300, 344,
    24, 400, 208, 48, 16, 32, 24,
};
#elif defined(ASSISTANT_BOARD_KEVIN_RGB)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    24, 328, 0, 52, 376, 240, 24, 24, 351, 31,
    324, 364, 500, 936,
    24, 24, 64, 54, 136, 32, 190, 280, 240, 832,
    24, 280, 332, 392,
    24, 856, 156, 48, 16, 48, 40,
};
#elif defined(ASSISTANT_BOARD_MVSRLORA)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    12, 198, 0, 40, 222, 136, 12, 12, 201, 19,
    190, 220, 170, 464,
    12, 22, 44, 54, 108, 28, 144, 228, 196, 396,
    12, 210, 250, 290,
    12, 416, 95, 44, 8, 28, 26,
};
#elif defined(ASSISTANT_BOARD_WS_CAM_35) || defined(ASSISTANT_BOARD_LCDWIKI)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 288, 0, 36, 320, 148, 16, 10, 299, 17,
    188, 216, 200, 464,
    12, 22, 44, 44, 96, 26, 96, 154, 134, 404,
    12, 154, 184, 212,
    16, 432, 140, 36, 8, 28, 26,
};
#elif defined(ASSISTANT_BOARD_GUITION)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    24, 432, 80, 26, 320, 100, 24, 8, 455, 15,
    132, 158, 26, 255,
    8, 22, 34, 44, 82, 24, 88, 132, 116, 214,
    8, 156, 180, 202,
    24, 224, 208, 40, 16, 24, 10,
};
#elif defined(ASSISTANT_BOARD_KORVO2)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 248, 0, 26, 280, 112, 16, 8, 259, 15,
    142, 168, 48, 231,
    8, 22, 34, 44, 82, 24, 88, 132, 116, 182,
    8, 124, 148, 170,
    16, 192, 120, 40, 8, 24, 10,
};
#elif defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_CHD)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 288, 0, 26, 320, 112, 16, 8, 299, 15,
    142, 168, 48, 231,
    8, 22, 34, 44, 82, 24, 88, 132, 116, 182,
    8, 124, 148, 170,
    16, 192, 140, 40, 8, 24, 10,
};
#elif defined(ASSISTANT_BOARD_STOPWATCH)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    76, 314, 49, 50, 368, 240, 140, 40, 230, 23,
    290, 322, 64, 410,
    68, 24, 106, 58, 176, 32, 184, 250, 216, 330,
    68, 278, 310, 334,
    96, 358, 128, 44, 18, 72, 80,
};
#elif defined(ASSISTANT_BOARD_LCD_154) || defined(ASSISTANT_BOARD_COMMUNITY_TFT)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 208, 0, 26, 240, 100, 16, 8, 218, 15,
    130, 158, 26, 232,
    8, 20, 34, 44, 82, 24, 86, 122, 114, 182,
    8, 122, 148, 170,
    16, 192, 100, 36, 8, 24, 10,
};
#elif defined(ASSISTANT_BOARD_LCD_183)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 208, 0, 32, 240, 128, 16, 10, 218, 17,
    164, 192, 56, 274,
    12, 22, 42, 44, 92, 24, 92, 148, 128, 224,
    12, 136, 166, 194,
    16, 232, 100, 40, 8, 28, 20,
};
#elif defined(ASSISTANT_BOARD_TCIRCLE)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    24, 112, 16, 30, 128, 64, 56, 12, 112, 18,
    94, 114, 20, 147,
    20, 18, 42, 20, 64, 20, 68, 68, 88, 104,
    20, 42, 64, 88,
    28, 108, 50, 28, 4, 20, 20,
};
#elif defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_CORES3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    16, 288, 0, 26, 320, 112, 16, 8, 299, 15,
    142, 168, board_profile::touch_listen ? 20 : 48, 231,
    8, 22, 34, 44, 82, 24, 88, 132, 116, 182,
    8, 124, 148, 170,
    16, 192, 140, 40, 8, 24, 10,
};
#elif defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C) || defined(ASSISTANT_BOARD_AMOLED_132)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    76, 314, 49, 50, 368, 240, 140, 40, 230, 23,
    290, 322, 64, 410,
    68, 24, 106, 58, 176, 32, 184, 250, 216, 330,
    68, 278, 310, 334,
    96, 358, 128, 44, 18, 72, 80,
};
#elif defined(ASSISTANT_BOARD_AMOLED_206)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    42, 326, 21, 70, 368, 240, 42, 30, 368, 37,
    318, 354, 78, 478,
    24, 28, 68, 68, 166, 40, 150, 256, 226, 378,
    66, 310, 348, 374,
    51, 414, 144, 48, 20, 64, 60,
};
#elif defined(ASSISTANT_BOARD_AMOLED_216) || defined(ASSISTANT_BOARD_M5_COREP4) || defined(ASSISTANT_BOARD_MOSAICO)
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    44, 392, 56, 62, 368, 240, 44, 28, 438, 35,
    306, 342, 72, 460,
    24, 28, 64, 68, 160, 40, 146, 248, 218, 358,
    62, 302, 340, 366,
    86, 402, 144, 48, 20, 64, 56,
};
#else
inline constexpr DisplayLayout DISPLAY_LAYOUT = {
    32, 304, 0, 64, 368, 240, 28, 26, 331, 33,
    308, 343, 67, 429,
    18, 34, 64, 68, 165, 48, 148, 245, 220, 344,
    64, 298, 334, 356,
    32, 375, 140, 48, 24, 65, 55,
};
#endif
