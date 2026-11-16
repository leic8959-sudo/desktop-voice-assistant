#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#if defined(ASSISTANT_BOARD_K10)
#include "digital_audio.h"
#include "k10_board.h"
#endif
#if defined(ASSISTANT_BOARD_MVSRLORA)
#include "digital_audio.h"
#include "mvsrlora_board.h"
#endif
#if defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_ELECTRON)
#include "digital_audio.h"
#endif
#if defined(ASSISTANT_BOARD_VOCAT)
#include "vocat_board.h"
#endif
#if defined(ASSISTANT_BOARD_LCD_EV)
#include "lcd_ev_board.h"
#endif
#if defined(ASSISTANT_BOARD_STOPWATCH)
#include "stopwatch_board.h"
#endif
#if defined(ASSISTANT_BOARD_CARDPUTER_ADV)
#include "cardputer_adv_keys.h"
#endif
#if defined(ASSISTANT_BOARD_WS349)
#include "waveshare_349_board.h"
#elif defined(ASSISTANT_BOARD_WS35)
#include "waveshare_35_board.h"
#elif defined(ASSISTANT_BOARD_WS4B)
#include "waveshare_4b_board.h"
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
#include "waveshare_rgb_c_board.h"
#endif
#if defined(ASSISTANT_BOARD_OTTO)
#include "otto_board.h"
#endif
#if defined(ASSISTANT_BOARD_WS_CAM)
#include "waveshare_cam_board.h"
#endif
#if defined(ASSISTANT_BOARD_KORVO2)
#include "korvo2_board.h"
#endif
#if defined(ASSISTANT_BOARD_FREENOVE)
#include "freenove_keys.h"
#endif
#if defined(ASSISTANT_BOARD_STICK_S3)
#include "stick_s3_power.h"
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
#include "atom_s3r_board.h"
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)
#include "atomic_echo_base.h"
#endif
#if defined(ASSISTANT_BOARD_ECHO_PYRAMID)
#include "echo_pyramid_board.h"
#endif
#if defined(ASSISTANT_BOARD_WS_AUDIO)
#include "waveshare_audio_board.h"
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX3)
#include "atk_box3_board.h"
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX2)
#include "i80_panel_io.h"
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX2)
#include "atk_box2_board.h"
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX)
#include "atk_box_board.h"
#include "digital_audio.h"
#endif
#if defined(ASSISTANT_BOARD_ATK_DEV)
#include "atk_xl9555.h"
#endif
#if defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
#include "pca9557_box.h"
#endif
#if defined(ASSISTANT_BOARD_XINGZHI_METAL)
#include "metal_keys.h"
#endif
#if defined(ASSISTANT_BOARD_TFT_DIGITAL)
#include "digital_audio.h"
#endif

#if defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_CHD) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_WS_RGB_C) || (defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT)))) || defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C) || defined(ASSISTANT_BOARD_STOPWATCH) || defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_P4_SPI35)
#define BSP_CAPS_TOUCH 1
#else
#define BSP_CAPS_TOUCH 0
#endif
#define BSP_CAPS_SDCARD 0

namespace spi_board {
#if defined(ASSISTANT_BOARD_P4_SPI35)
inline constexpr int width = 320, height = 480;
#elif defined(ASSISTANT_BOARD_K10)
inline constexpr int width = 240, height = 320;
#elif defined(ASSISTANT_BOARD_MVSRLORA)
inline constexpr int width = 222, height = 480;
#elif defined(ASSISTANT_BOARD_VOCAT)
inline constexpr int width = 360, height = 360;
#elif defined(ASSISTANT_BOARD_LCD_EV)
#if defined(ASSISTANT_BOARD_LCD_EV_2)
inline constexpr int width = 800, height = 480;
#else
inline constexpr int width = 480, height = 480;
#endif
#elif defined(ASSISTANT_BOARD_STOPWATCH)
inline constexpr int width = 466, height = 466;
#elif defined(ASSISTANT_BOARD_CARDPUTER_ADV)
inline constexpr int width = 240, height = 135;
#elif defined(ASSISTANT_BOARD_ATOM_S3_BASE)
inline constexpr int width = 128, height = 128;
#elif defined(ASSISTANT_BOARD_WS143C)
inline constexpr int width = 466, height = 466;
#elif defined(ASSISTANT_BOARD_WS349)
inline constexpr int width = 172, height = 640;
#elif defined(ASSISTANT_BOARD_WS35)
inline constexpr int width = 480, height = 320;
#elif defined(ASSISTANT_BOARD_WS4B)
inline constexpr int width = 480, height = 480;
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
inline constexpr int width = 800, height = 480;
#elif defined(ASSISTANT_BOARD_KEVIN_RGB)
inline constexpr int width = 376, height = 960;
#elif defined(ASSISTANT_BOARD_LCDWIKI)
inline constexpr int width = 320, height = 480;
#elif defined(ASSISTANT_BOARD_WS_CAM_183)
inline constexpr int width = 240, height = 284;
#elif defined(ASSISTANT_BOARD_WS_CAM_35)
inline constexpr int width = 320, height = 480;
#elif defined(ASSISTANT_BOARD_WS_CAM)
inline constexpr int width = 240, height = 320;
#elif defined(ASSISTANT_BOARD_KORVO2)
inline constexpr int width = 280, height = 240;
#elif defined(ASSISTANT_BOARD_STICK_S3)
inline constexpr int width = 135, height = 240;
#elif defined(ASSISTANT_BOARD_CHD_LCD35)
inline constexpr int width = 480, height = 320;
#elif defined(ASSISTANT_BOARD_CHD) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
inline constexpr int width = 320, height = 240;
#elif defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_LABPLUS)
inline constexpr int width = 320, height = 172;
#elif defined(ASSISTANT_BOARD_ATK_BOX2)
inline constexpr int width = 240, height = 320;
#elif defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ATK_BOX3)
inline constexpr int width = 320, height = 240;
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
inline constexpr int width = 240, height = 296;
#elif defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_MIXGO)
inline constexpr int width = 128, height = 160;
#elif defined(ASSISTANT_BOARD_KEVIN_SPV4)
inline constexpr int width = 240, height = 280;
#elif defined(ASSISTANT_BOARD_YUNLIAO)
inline constexpr int width = 320, height = 240;
#elif defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
inline constexpr int width = 128, height = 128;
#else
inline constexpr int width = 240;
#if defined(ASSISTANT_BOARD_BREAD_TFT)
inline constexpr int height = 320;
#else
inline constexpr int height = 240;
#endif
#endif
#if defined(ASSISTANT_BOARD_P4_SPI35)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_7, i2c_scl = GPIO_NUM_8;
inline constexpr gpio_num_t mclk = GPIO_NUM_13, bclk = GPIO_NUM_12, word_select = GPIO_NUM_10;
inline constexpr gpio_num_t audio_input = GPIO_NUM_11, audio_output = GPIO_NUM_9, amplifier = GPIO_NUM_53;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_20, lcd_clock = GPIO_NUM_21, lcd_cs = GPIO_NUM_23;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_26, lcd_reset = GPIO_NUM_27, backlight = GPIO_NUM_28;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_29, touch_interrupt = GPIO_NUM_50;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000, touch_clock_hz = 400000;
#elif defined(ASSISTANT_BOARD_ELECTRON)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_10, lcd_clock = GPIO_NUM_11, lcd_cs = GPIO_NUM_12;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_13, lcd_reset = GPIO_NUM_9, backlight = GPIO_NUM_46;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_K10)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_21, lcd_clock = GPIO_NUM_12, lcd_cs = GPIO_NUM_14;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_13, lcd_reset = GPIO_NUM_NC;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_MVSRLORA)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_5, i2c_scl = GPIO_NUM_6;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_17, lcd_clock = GPIO_NUM_18, lcd_cs = GPIO_NUM_39;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_9, lcd_reset = GPIO_NUM_47, backlight = GPIO_NUM_48;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_TCAMERA)
// Electrical facts checked against both manufacturer revisions;no vendor
// implementation or GPL pin header is copied into this local glue.
inline constexpr gpio_num_t backlight = GPIO_NUM_46, lcd_dc = GPIO_NUM_45;
inline constexpr int spi_mode = 0, spi_clock_hz = 60000000, touch_clock_hz = 100000;
#if defined(ASSISTANT_BOARD_TCAMERA_V12)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_33, i2c_scl = GPIO_NUM_37;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_34, lcd_clock = GPIO_NUM_35, lcd_cs = GPIO_NUM_36, lcd_reset = GPIO_NUM_NC;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_NC;
#else
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_1, i2c_scl = GPIO_NUM_2;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_35, lcd_clock = GPIO_NUM_36, lcd_cs = GPIO_NUM_34, lcd_reset = GPIO_NUM_33;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_48;
#endif
// Polling requires noGPIO47IRQ;oldrevision also names47asSY6970INT.
inline constexpr gpio_num_t touch_interrupt = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_VOCAT)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_2, i2c_scl = GPIO_NUM_1;
inline constexpr gpio_num_t mclk = GPIO_NUM_42, bclk = GPIO_NUM_40, word_select = GPIO_NUM_39;
// Revision detection owns DIN15/3 and PA4/15;do not drive either candidate early.
inline gpio_num_t audio_input = GPIO_NUM_NC;
inline constexpr gpio_num_t audio_output = GPIO_NUM_41, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_LCD_EV)
#if defined(ASSISTANT_BOARD_LCD_EV_14)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_18;
#else
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
#endif
inline constexpr gpio_num_t mclk = GPIO_NUM_5, bclk = GPIO_NUM_16, word_select = GPIO_NUM_7;
inline constexpr gpio_num_t audio_input = GPIO_NUM_15, audio_output = GPIO_NUM_6, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_STOPWATCH)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_18, bclk = GPIO_NUM_17, word_select = GPIO_NUM_15;
inline constexpr gpio_num_t audio_input = GPIO_NUM_16, audio_output = GPIO_NUM_21, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_SPARKBOT)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_4, i2c_scl = GPIO_NUM_5;
inline constexpr gpio_num_t mclk = GPIO_NUM_45, bclk = GPIO_NUM_39, word_select = GPIO_NUM_41;
// PA46 shares backlight. The display owns its stable level; codec never gates it.
inline constexpr gpio_num_t audio_input = GPIO_NUM_40, audio_output = GPIO_NUM_42, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_44, lcd_dc = GPIO_NUM_43, lcd_reset = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_47, lcd_clock = GPIO_NUM_21, backlight = GPIO_NUM_46;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_CARDPUTER_ADV)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_9;
inline constexpr gpio_num_t mclk = GPIO_NUM_NC, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t bclk = GPIO_NUM_41, word_select = GPIO_NUM_43;
inline constexpr gpio_num_t audio_input = GPIO_NUM_46, audio_output = GPIO_NUM_42;
#elif defined(ASSISTANT_BOARD_ATOM_S3_BASE)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_38, i2c_scl = GPIO_NUM_39;
inline constexpr gpio_num_t mclk = GPIO_NUM_NC, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t bclk = GPIO_NUM_8, word_select = GPIO_NUM_6;
inline constexpr gpio_num_t audio_input = GPIO_NUM_7, audio_output = GPIO_NUM_5;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_15, lcd_dc = GPIO_NUM_33, lcd_reset = GPIO_NUM_34;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_21, lcd_clock = GPIO_NUM_17;
#elif defined(ASSISTANT_BOARD_WS143C)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_38, bclk = GPIO_NUM_39, word_select = GPIO_NUM_40;
inline constexpr gpio_num_t audio_input = GPIO_NUM_42, audio_output = GPIO_NUM_41, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_WS349)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_7, bclk = GPIO_NUM_15, word_select = GPIO_NUM_46;
inline constexpr gpio_num_t audio_input = GPIO_NUM_6, audio_output = GPIO_NUM_45, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_WS35)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_7;
#if defined(ASSISTANT_BOARD_WS35B)
inline constexpr gpio_num_t mclk = GPIO_NUM_44;
#else
inline constexpr gpio_num_t mclk = GPIO_NUM_12;
#endif
inline constexpr gpio_num_t bclk = GPIO_NUM_13, word_select = GPIO_NUM_15;
inline constexpr gpio_num_t audio_input = GPIO_NUM_14, audio_output = GPIO_NUM_16, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_WS4B)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_5, bclk = GPIO_NUM_16, word_select = GPIO_NUM_7;
inline constexpr gpio_num_t audio_input = GPIO_NUM_15, audio_output = GPIO_NUM_6, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
#if defined(ASSISTANT_BOARD_WS_RGB_7C)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
#else
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_9;
#endif
inline constexpr gpio_num_t mclk = GPIO_NUM_6, bclk = GPIO_NUM_44, word_select = GPIO_NUM_16;
inline constexpr gpio_num_t audio_input = GPIO_NUM_43, audio_output = GPIO_NUM_15, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_KEVIN_RGB)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_1, i2c_scl = GPIO_NUM_2;
inline constexpr gpio_num_t mclk = GPIO_NUM_42, bclk = GPIO_NUM_41, word_select = GPIO_NUM_39;
inline constexpr gpio_num_t audio_input = GPIO_NUM_40, audio_output = GPIO_NUM_38, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_LCDWIKI)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_38, i2c_scl = GPIO_NUM_39;
inline constexpr gpio_num_t mclk = GPIO_NUM_17, bclk = GPIO_NUM_18, word_select = GPIO_NUM_21;
// External active-low PA1 is gated after the actual DAC mute callback.
inline constexpr gpio_num_t audio_input = GPIO_NUM_16, audio_output = GPIO_NUM_15, amplifier = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_OTTO)
// Assigned once after the board's GPIO15/16 hardware detection, before SPI.
inline gpio_num_t lcd_mosi = GPIO_NUM_NC, lcd_clock = GPIO_NUM_NC, lcd_dc = GPIO_NUM_NC;
inline gpio_num_t lcd_cs = GPIO_NUM_NC, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_NC;
inline constexpr int spi_mode = 3, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_EDA_TV)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_11, lcd_clock = GPIO_NUM_12, lcd_dc = GPIO_NUM_47;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_NC, lcd_reset = GPIO_NUM_48, backlight = GPIO_NUM_38;
// Keep the actual board's mode2; its 7PIN menu name is not mode3 evidence.
inline constexpr int spi_mode = 2, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_WS_CAM)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_7;
inline constexpr gpio_num_t mclk = GPIO_NUM_10, bclk = GPIO_NUM_11, word_select = GPIO_NUM_12;
inline constexpr gpio_num_t audio_input = GPIO_NUM_13, audio_output = GPIO_NUM_14, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_1, lcd_clock = GPIO_NUM_5, lcd_dc = GPIO_NUM_3;
// Actual board class leaves MISO unused despite its config.h macro forGPIO2.
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_6, lcd_reset = GPIO_NUM_NC;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_BREAD_S3_CAM)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_20, lcd_clock = GPIO_NUM_19, lcd_dc = GPIO_NUM_47;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_45, lcd_reset = GPIO_NUM_21, backlight = GPIO_NUM_38;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_1, i2c_scl = GPIO_NUM_2;
inline constexpr gpio_num_t mclk = GPIO_NUM_38, bclk = GPIO_NUM_14, word_select = GPIO_NUM_13;
inline constexpr gpio_num_t audio_input = GPIO_NUM_12, audio_output = GPIO_NUM_45, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_40, lcd_clock = GPIO_NUM_41, lcd_dc = GPIO_NUM_39;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_NC, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_42;
inline constexpr int spi_mode = 0, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_KORVO2)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_17, i2c_scl = GPIO_NUM_18;
inline constexpr gpio_num_t mclk = GPIO_NUM_16, bclk = GPIO_NUM_9, word_select = GPIO_NUM_45;
inline constexpr gpio_num_t audio_input = GPIO_NUM_10, audio_output = GPIO_NUM_8, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_0, lcd_clock = GPIO_NUM_1, lcd_dc = GPIO_NUM_2;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_46, lcd_reset = GPIO_NUM_NC;
inline constexpr int spi_mode = 0, spi_clock_hz = 60000000;
#elif defined(ASSISTANT_BOARD_QUANDONG)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_16, i2c_scl = GPIO_NUM_15;
inline constexpr gpio_num_t mclk = GPIO_NUM_4, bclk = GPIO_NUM_5, word_select = GPIO_NUM_7;
inline constexpr gpio_num_t audio_input = GPIO_NUM_6, audio_output = GPIO_NUM_8, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_11, lcd_clock = GPIO_NUM_12, lcd_dc = GPIO_NUM_46;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_10, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_45;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_FREENOVE)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_16, i2c_scl = GPIO_NUM_15;
inline constexpr gpio_num_t mclk = GPIO_NUM_4, bclk = GPIO_NUM_5, word_select = GPIO_NUM_7;
// PA1 is active low and gated explicitly outside codec_dev.
inline constexpr gpio_num_t audio_input = GPIO_NUM_6, audio_output = GPIO_NUM_8, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_11, lcd_miso = GPIO_NUM_13, lcd_clock = GPIO_NUM_12, lcd_dc = GPIO_NUM_46;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_10, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_45;
inline constexpr int spi_mode = 0, spi_clock_hz = 20000000;
#elif defined(ASSISTANT_BOARD_CHD_EYE)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_4, i2c_scl = GPIO_NUM_5;
inline constexpr gpio_num_t mclk = GPIO_NUM_2, bclk = GPIO_NUM_42, word_select = GPIO_NUM_45;
inline constexpr gpio_num_t audio_input = GPIO_NUM_41, audio_output = GPIO_NUM_46, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_47, lcd_clock = GPIO_NUM_21, lcd_dc = GPIO_NUM_43;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_44, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_48;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_STICK_S3)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_18, bclk = GPIO_NUM_17, word_select = GPIO_NUM_15;
inline constexpr gpio_num_t audio_input = GPIO_NUM_16, audio_output = GPIO_NUM_14, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_39, lcd_clock = GPIO_NUM_40, lcd_dc = GPIO_NUM_45;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_41, lcd_reset = GPIO_NUM_21, backlight = GPIO_NUM_38;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_38, i2c_scl = GPIO_NUM_39;
inline constexpr gpio_num_t mclk = GPIO_NUM_NC, amplifier = GPIO_NUM_NC;
#if defined(ASSISTANT_BOARD_ECHO_PYRAMID)
inline constexpr gpio_num_t bclk = GPIO_NUM_6, word_select = GPIO_NUM_8;
inline constexpr gpio_num_t audio_input = GPIO_NUM_5, audio_output = GPIO_NUM_7;
inline constexpr bool gc9107_mirror = true;
inline constexpr int gc9107_gap_y = 0;
#else
inline constexpr gpio_num_t bclk = GPIO_NUM_8, word_select = GPIO_NUM_6;
inline constexpr gpio_num_t audio_input = GPIO_NUM_7, audio_output = GPIO_NUM_5;
inline constexpr bool gc9107_mirror = false;
inline constexpr int gc9107_gap_y = 32;
#endif
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_21, lcd_clock = GPIO_NUM_15, lcd_dc = GPIO_NUM_42;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_14, lcd_reset = GPIO_NUM_48;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_CHD)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_18;
inline constexpr gpio_num_t mclk = GPIO_NUM_2, bclk = GPIO_NUM_17, word_select = GPIO_NUM_45;
inline constexpr gpio_num_t audio_input = GPIO_NUM_16, audio_output = GPIO_NUM_15, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_6, lcd_clock = GPIO_NUM_7, lcd_dc = GPIO_NUM_4;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_5, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_47;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_LABPLUS)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_44, i2c_scl = GPIO_NUM_43;
inline constexpr gpio_num_t mclk = GPIO_NUM_39, bclk = GPIO_NUM_41, word_select = GPIO_NUM_42;
inline constexpr gpio_num_t audio_input = GPIO_NUM_40, audio_output = GPIO_NUM_38, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_37, lcd_clock = GPIO_NUM_36, lcd_dc = GPIO_NUM_35, lcd_reset = GPIO_NUM_NC;
#if defined(ASSISTANT_BOARD_LEDONG)
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_NC, backlight = GPIO_NUM_34;
#else
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_34, backlight = GPIO_NUM_33;
#endif
inline constexpr int spi_mode = 0, spi_clock_hz = 20000000;
#elif defined(ASSISTANT_BOARD_MIXGO)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_37, i2c_scl = GPIO_NUM_36;
inline constexpr gpio_num_t mclk = GPIO_NUM_35, bclk = GPIO_NUM_34, word_select = GPIO_NUM_47;
inline constexpr gpio_num_t audio_input = GPIO_NUM_33, audio_output = GPIO_NUM_48, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_40, lcd_clock = GPIO_NUM_41, lcd_dc = GPIO_NUM_18;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_45, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_14;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_MAGICLICK)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_5, i2c_scl = GPIO_NUM_6;
inline constexpr gpio_num_t mclk = GPIO_NUM_8, bclk = GPIO_NUM_9, word_select = GPIO_NUM_11;
inline constexpr gpio_num_t audio_input = GPIO_NUM_10, audio_output = GPIO_NUM_12, amplifier = GPIO_NUM_4;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_18, backlight = GPIO_NUM_13;
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_16, lcd_clock = GPIO_NUM_15, lcd_cs = GPIO_NUM_14, lcd_reset = GPIO_NUM_17;
inline constexpr int spi_mode = 0, spi_clock_hz = 20000000;
#else
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_15, lcd_clock = GPIO_NUM_16, lcd_cs = GPIO_NUM_17, lcd_reset = GPIO_NUM_14;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#endif
#elif defined(ASSISTANT_BOARD_MINSI)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_17, lcd_clock = GPIO_NUM_8, lcd_dc = GPIO_NUM_15;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_NC, lcd_reset = GPIO_NUM_16, backlight = GPIO_NUM_7;
inline constexpr int spi_mode = 3, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_AIVOX3)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_13, i2c_scl = GPIO_NUM_12;
inline constexpr gpio_num_t mclk = GPIO_NUM_11, bclk = GPIO_NUM_10, word_select = GPIO_NUM_8;
inline constexpr gpio_num_t audio_input = GPIO_NUM_9, audio_output = GPIO_NUM_7, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_21, lcd_clock = GPIO_NUM_17, lcd_dc = GPIO_NUM_14;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_15, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_16;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_WS_AUDIO)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_11, i2c_scl = GPIO_NUM_10;
inline constexpr gpio_num_t mclk = GPIO_NUM_12, bclk = GPIO_NUM_13, word_select = GPIO_NUM_14;
inline constexpr gpio_num_t audio_input = GPIO_NUM_15, audio_output = GPIO_NUM_16, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_9, lcd_clock = GPIO_NUM_4, lcd_dc = GPIO_NUM_7;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_3, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_5;
inline constexpr int spi_mode = 0, spi_clock_hz = 20000000;
#elif defined(ASSISTANT_BOARD_AIPI)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_5, i2c_scl = GPIO_NUM_4;
inline constexpr gpio_num_t mclk = GPIO_NUM_6, bclk = GPIO_NUM_14, word_select = GPIO_NUM_12;
inline constexpr gpio_num_t audio_input = GPIO_NUM_13, audio_output = GPIO_NUM_11, amplifier = GPIO_NUM_9;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_17, lcd_clock = GPIO_NUM_16, lcd_dc = GPIO_NUM_7;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_15, lcd_reset = GPIO_NUM_18, backlight = GPIO_NUM_3;
// Actual fixed board class uses40MHz, overriding its unused20MHz macro.
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_ATK_BOX3)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_3, i2c_scl = GPIO_NUM_2;
inline constexpr gpio_num_t mclk = GPIO_NUM_21, bclk = GPIO_NUM_38, word_select = GPIO_NUM_39;
inline constexpr gpio_num_t audio_input = GPIO_NUM_41, audio_output = GPIO_NUM_40, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_16, lcd_miso = GPIO_NUM_17, lcd_clock = GPIO_NUM_15, lcd_dc = GPIO_NUM_48;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_47, lcd_reset = GPIO_NUM_NC;
inline constexpr int spi_mode = 0, spi_clock_hz = 60000000;
#elif defined(ASSISTANT_BOARD_ATK_BOX2)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_48, i2c_scl = GPIO_NUM_47;
inline constexpr gpio_num_t mclk = GPIO_NUM_38, bclk = GPIO_NUM_40, word_select = GPIO_NUM_42;
inline constexpr gpio_num_t audio_input = GPIO_NUM_39, audio_output = GPIO_NUM_41, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_21;
inline constexpr I80PanelIoConfig panel_io = {10, 12, 11, 14, {13, 9, 8, 7, 6, 5, 4, 3}, 240, 20000000, 7, true};
#elif defined(ASSISTANT_BOARD_ATK_BOX)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_48, i2c_scl = GPIO_NUM_45;
inline constexpr gpio_num_t mclk = GPIO_NUM_NC, bclk = GPIO_NUM_21, word_select = GPIO_NUM_13;
inline constexpr gpio_num_t audio_input = GPIO_NUM_47, audio_output = GPIO_NUM_14, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_reset = GPIO_NUM_NC;
inline constexpr I80PanelIoConfig panel_io = {41, 2, 42, 1, {40, 39, 38, 12, 11, 10, 9, 46}, 320, 10000000, 10, false};
#elif defined(ASSISTANT_BOARD_ATK_BOX0)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_11, i2c_scl = GPIO_NUM_12;
inline constexpr gpio_num_t mclk = GPIO_NUM_NC, bclk = GPIO_NUM_5, word_select = GPIO_NUM_10;
inline constexpr gpio_num_t audio_input = GPIO_NUM_6, audio_output = GPIO_NUM_9, amplifier = GPIO_NUM_21;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_40, lcd_clock = GPIO_NUM_39, lcd_dc = GPIO_NUM_38;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_41, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_42;
inline constexpr int spi_mode = 0, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_ATK_DEV)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_41, i2c_scl = GPIO_NUM_42;
inline constexpr gpio_num_t mclk = GPIO_NUM_3, bclk = GPIO_NUM_46, word_select = GPIO_NUM_9;
inline constexpr gpio_num_t audio_input = GPIO_NUM_14, audio_output = GPIO_NUM_10, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_11, lcd_clock = GPIO_NUM_12, lcd_dc = GPIO_NUM_40;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_21, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_NC;
inline constexpr int spi_mode = 0, spi_clock_hz = 20000000;
#elif defined(ASSISTANT_BOARD_PCA_BOX)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_1, i2c_scl = GPIO_NUM_2;
inline constexpr gpio_num_t mclk = GPIO_NUM_38, bclk = GPIO_NUM_14, word_select = GPIO_NUM_13;
inline constexpr gpio_num_t audio_input = GPIO_NUM_12, audio_output = GPIO_NUM_45, amplifier = GPIO_NUM_NC;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_40, lcd_clock = GPIO_NUM_41, lcd_dc = GPIO_NUM_39;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_NC, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_42;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_NC, touch_interrupt = GPIO_NUM_NC;
inline constexpr int spi_mode = 2, spi_clock_hz = 80000000, touch_clock_hz = 400000;
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_1, i2c_scl = GPIO_NUM_2;
inline constexpr gpio_num_t mclk = GPIO_NUM_38, bclk = GPIO_NUM_14, word_select = GPIO_NUM_13;
inline constexpr gpio_num_t audio_input = GPIO_NUM_11, audio_output = GPIO_NUM_12, amplifier = GPIO_NUM_42;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_40, lcd_clock = GPIO_NUM_41, lcd_dc = GPIO_NUM_39;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_9, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_46;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_DUCHATX)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_18, lcd_clock = GPIO_NUM_17, lcd_dc = GPIO_NUM_8;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_16, lcd_reset = GPIO_NUM_20, backlight = GPIO_NUM_9;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_KEVIN_SPV4)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_4, i2c_scl = GPIO_NUM_5;
inline constexpr gpio_num_t mclk = GPIO_NUM_42, bclk = GPIO_NUM_46, word_select = GPIO_NUM_1;
inline constexpr gpio_num_t audio_input = GPIO_NUM_2, audio_output = GPIO_NUM_3, amplifier = GPIO_NUM_41;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_47, lcd_clock = GPIO_NUM_21, lcd_dc = GPIO_NUM_45;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_14, lcd_reset = GPIO_NUM_NC, backlight = GPIO_NUM_48;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_YUNLIAO)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_18, i2c_scl = GPIO_NUM_21;
inline constexpr gpio_num_t mclk = GPIO_NUM_14, bclk = GPIO_NUM_13, word_select = GPIO_NUM_11;
inline constexpr gpio_num_t audio_input = GPIO_NUM_10, audio_output = GPIO_NUM_12, amplifier = GPIO_NUM_17;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_40, lcd_clock = GPIO_NUM_42, lcd_dc = GPIO_NUM_41;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_NC, lcd_reset = GPIO_NUM_45, backlight = GPIO_NUM_46;
inline constexpr int spi_mode = 3, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_LC_TFT)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_7, i2c_scl = GPIO_NUM_6;
inline constexpr gpio_num_t mclk = GPIO_NUM_5, bclk = GPIO_NUM_4, word_select = GPIO_NUM_2;
inline constexpr gpio_num_t audio_input = GPIO_NUM_3, audio_output = GPIO_NUM_1, amplifier = GPIO_NUM_8;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_15, lcd_clock = GPIO_NUM_14, lcd_dc = GPIO_NUM_10;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_13, lcd_reset = GPIO_NUM_16, backlight = GPIO_NUM_21;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_SPOTPEAR)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_15, i2c_scl = GPIO_NUM_14;
inline constexpr gpio_num_t mclk = GPIO_NUM_16, bclk = GPIO_NUM_9, word_select = GPIO_NUM_45;
inline constexpr gpio_num_t audio_input = GPIO_NUM_10, audio_output = GPIO_NUM_8, amplifier = GPIO_NUM_46;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_2, lcd_clock = GPIO_NUM_4, lcd_dc = GPIO_NUM_47;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_5, lcd_reset = GPIO_NUM_38, backlight = GPIO_NUM_42;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_6, touch_interrupt = GPIO_NUM_12;
inline constexpr int touch_clock_hz = 400000, spi_mode = 0;
#if defined(ASSISTANT_BOARD_SPOTPEAR_BOX)
inline constexpr int spi_clock_hz = 40000000;
#else
inline constexpr int spi_clock_hz = 60000000;
#endif
#elif defined(ASSISTANT_BOARD_MOVECALL_MOJI)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_5, i2c_scl = GPIO_NUM_4;
inline constexpr gpio_num_t mclk = GPIO_NUM_6, bclk = GPIO_NUM_14, word_select = GPIO_NUM_12;
inline constexpr gpio_num_t audio_input = GPIO_NUM_13, audio_output = GPIO_NUM_11, amplifier = GPIO_NUM_9;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_17, lcd_clock = GPIO_NUM_16, lcd_dc = GPIO_NUM_7;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_15, lcd_reset = GPIO_NUM_18, backlight = GPIO_NUM_3;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_MOVECALL_CUICAN)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_6, i2c_scl = GPIO_NUM_7;
inline constexpr gpio_num_t mclk = GPIO_NUM_45, bclk = GPIO_NUM_39, word_select = GPIO_NUM_41;
inline constexpr gpio_num_t audio_input = GPIO_NUM_40, audio_output = GPIO_NUM_42, amplifier = GPIO_NUM_17;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_10, lcd_clock = GPIO_NUM_12, lcd_dc = GPIO_NUM_14;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_13, lcd_reset = GPIO_NUM_11, backlight = GPIO_NUM_16;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_GENJUTECH)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_9, i2c_scl = GPIO_NUM_8;
inline constexpr gpio_num_t mclk = GPIO_NUM_14, bclk = GPIO_NUM_13, word_select = GPIO_NUM_11;
inline constexpr gpio_num_t audio_input = GPIO_NUM_12, audio_output = GPIO_NUM_10, amplifier = GPIO_NUM_21;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_3, lcd_clock = GPIO_NUM_4, lcd_dc = GPIO_NUM_5;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_6, lcd_reset = GPIO_NUM_7, backlight = GPIO_NUM_2;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_XINGZHI_ABS) || defined(ASSISTANT_BOARD_XINGZHI_METAL)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_41, i2c_scl = GPIO_NUM_42;
inline constexpr gpio_num_t mclk = GPIO_NUM_5, bclk = GPIO_NUM_6, word_select = GPIO_NUM_4;
inline constexpr gpio_num_t audio_input = GPIO_NUM_17, audio_output = GPIO_NUM_15, amplifier = GPIO_NUM_21;
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_10, lcd_clock = GPIO_NUM_9, lcd_dc = GPIO_NUM_8;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_14, lcd_reset = GPIO_NUM_18, backlight = GPIO_NUM_13;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_XINGZHI_085)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_10, lcd_clock = GPIO_NUM_9, lcd_dc = GPIO_NUM_8;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_14, lcd_reset = GPIO_NUM_18, backlight = GPIO_NUM_13;
// Pinned NV3023 component's public SPI IO default; preserve its mode/rate.
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_XINGZHI_TFT)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_10, lcd_clock = GPIO_NUM_9, lcd_dc = GPIO_NUM_8;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_14, lcd_reset = GPIO_NUM_18, backlight = GPIO_NUM_13;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000;
#elif defined(ASSISTANT_BOARD_BREAD_TFT)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_47, lcd_clock = GPIO_NUM_21, lcd_dc = GPIO_NUM_40;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_41, lcd_reset = GPIO_NUM_45, backlight = GPIO_NUM_42;
inline constexpr int spi_mode = 0, spi_clock_hz = 40000000;
#elif defined(ASSISTANT_BOARD_ZHENGCHEN_TFT)
inline constexpr gpio_num_t lcd_mosi = GPIO_NUM_41, lcd_clock = GPIO_NUM_42, lcd_dc = GPIO_NUM_40;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_21, lcd_reset = GPIO_NUM_45, backlight = GPIO_NUM_20;
inline constexpr int spi_mode = 3, spi_clock_hz = 80000000;
#else
#error "No implemented community TFT board selected"
#endif
}

#if defined(ASSISTANT_BOARD_LC_TFT) || defined(ASSISTANT_BOARD_XINGZHI_ABS) || defined(ASSISTANT_BOARD_XINGZHI_METAL) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_GENJUTECH) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_LCDWIKI) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_ATOM_S3_BASE) || defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_STOPWATCH) || defined(ASSISTANT_BOARD_SPARKBOT) || defined(ASSISTANT_BOARD_P4_SPI35)
// The existing single-ES8311 engine opens one bidirectional codec handle.
#if defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE) || defined(ASSISTANT_BOARD_P4_SPI35)
#define BSP_I2C_NUM I2C_NUM_1
#else
#define BSP_I2C_NUM I2C_NUM_0
#endif
#define BSP_I2S_MCLK spi_board::mclk
#define BSP_I2S_SCLK spi_board::bclk
#define BSP_I2S_LCLK spi_board::word_select
#define BSP_I2S_DSIN spi_board::audio_input
#define BSP_I2S_DOUT spi_board::audio_output
#define BSP_POWER_AMP_IO spi_board::amplifier
#endif

esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
#if defined(ASSISTANT_BOARD_SPOTPEAR)
bool bsp_display_touch_available();
#endif
#if defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || (defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)) || defined(ASSISTANT_BOARD_KORVO2) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_ECHO_PYRAMID)
#include "esp_codec_dev.h"
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
#if defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || (defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)) || defined(ASSISTANT_BOARD_KORVO2) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_ECHO_PYRAMID)
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
#endif
#endif

#if defined(ASSISTANT_BOARD_WS_RGB_7C)
int waveshare_spi_audio_write_mono(esp_codec_dev_handle_t speaker, uint8_t *data, size_t length);
#endif
