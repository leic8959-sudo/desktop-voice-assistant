#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "manual_input.h"

#if defined(ASSISTANT_BOARD_SENSAIR_C5)
#include "sensair_board.h"
#elif defined(ASSISTANT_BOARD_MOJI2_C5)
#include "moji2_c5_board.h"
#elif defined(ASSISTANT_BOARD_C5_LCD169)
#include "c5_lcd169_board.h"
#elif defined(ASSISTANT_BOARD_S31_KORVO)
#include "s31_korvo_board.h"
#elif defined(ASSISTANT_BOARD_MOSAICO)
#include "mosaico_board.h"
#elif defined(ASSISTANT_BOARD_M5_COREP4)
#include "m5_corep4_board.h"
#elif defined(ASSISTANT_BOARD_M5_TAB5)
#include "m5_tab5_board.h"
#elif defined(ASSISTANT_BOARD_P4_MIPI)
#include "waveshare_p4_mipi_board.h"
#elif defined(ASSISTANT_BOARD_P4_FUNCTION)
#include "bsp/esp32_p4_function_ev_board.h"
#elif defined(ASSISTANT_BOARD_SENSECAP)
#include "sensecap_board.h"
#elif defined(ASSISTANT_BOARD_MATRIX)
#include "matrix_board.h"
#elif defined(ASSISTANT_BOARD_WS_RLCD)
#include "rlcd_board.h"
#elif defined(ASSISTANT_BOARD_HEADLESS)
#include "headless_board.h"
#elif defined(ASSISTANT_BOARD_MAZA) || defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_WS_EPAPER397)
#include "paper_board.h"
#elif defined(ASSISTANT_BOARD_OLED)
#include "oled_board.h"
#elif defined(ASSISTANT_BOARD_GUITION)
#include "guition_board.h"
#elif defined(ASSISTANT_BOARD_COMMUNITY_TFT)
#include "tft_board.h"
#elif defined(ASSISTANT_BOARD_TCIRCLE)
#include "t_circle_board.h"
#elif defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185_FAMILY) || defined(ASSISTANT_BOARD_TAIJI)
#include "waveshare_qspi_board.h"
#elif defined(ASSISTANT_BOARD_WAVESHARE_SPI)
#include "waveshare_spi_board.h"
#elif defined(ASSISTANT_BOARD_AMOLED_132)
#include "amoled216_board.h"
#elif defined(ASSISTANT_BOARD_AMOLED_206)
#include "amoled216_board.h"
#elif defined(ASSISTANT_BOARD_AMOLED_216)
#include "amoled216_board.h"
#elif defined(ASSISTANT_BOARD_BOX3)
#include "box3_board.h"
#elif defined(ASSISTANT_BOARD_BOX)
#include "box3_board.h"
#elif defined(ASSISTANT_BOARD_BOXLITE)
#include "box3_board.h"
#elif defined(ASSISTANT_BOARD_AMOLED_175)
#include "amoled216_board.h"
#elif defined(ASSISTANT_BOARD_AMOLED_175C)
#include "amoled216_board.h"
#elif defined(ASSISTANT_BOARD_CORES3)
#include "cores3_board.h"
#elif defined(ASSISTANT_BOARD_SH8601)
#include "amoled216_board.h"
#elif defined(ASSISTANT_BOARD_AMOLED_18)
#include "bsp/esp32_s3_touch_amoled_1_8.h"
#else
#error "No implemented native board profile selected"
#endif

namespace board_profile {
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
inline constexpr const char *name = "espressif-esp-sensairshuttle";
inline constexpr int width = 284, height = 240;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_28;
#elif defined(ASSISTANT_BOARD_SPOT_C5)
inline constexpr const char *name = "espressif-esp-spot-c5";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_5;
#elif defined(ASSISTANT_BOARD_MOJI2_C5)
inline constexpr const char *name = "movecall-moji2-esp32c5";
inline constexpr int width = 360, height = 360;
// Use the existing360-pixel rounded layout so controls stay inside the inset.
inline constexpr bool round = true, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_28;
#elif defined(ASSISTANT_BOARD_C5_LCD169)
inline constexpr const char *name = "waveshare-esp32-c5-touch-lcd-1.69";
inline constexpr int width = 240, height = 280;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_28;
#elif defined(ASSISTANT_BOARD_S31_KORVO)
inline constexpr const char *name = "espressif-esp32-s31-korvo-1";
inline constexpr int width = 800, height = 480;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_61;
#elif defined(ASSISTANT_BOARD_MOSAICO)
inline constexpr const char *name = "espressif-esp-mosaico";
inline constexpr int width = 480, height = 480;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_7;
#elif defined(ASSISTANT_BOARD_M5_COREP4)
inline constexpr const char *name = "m5stack-corep4x";
inline constexpr int width = 480, height = 480;
inline constexpr bool round = false, separate_microphone = true;
// GPIO0 is also documented as IMU INT; use touch controls until the physical
// key wiring is confirmed rather than treating motion IRQs as push-to-talk.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_M5_TAB5)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "m5stack-tab5";
#else
inline constexpr const char *name = "m5stack-tab5-p4x";
#endif
inline constexpr int width = 720, height = 1280;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_P4_NANO)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-nano-10.1-a";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-nano-10.1-a";
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_JD9365)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
#if defined(ASSISTANT_BOARD_P4_JD9365_34C)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-3.4c";
#elif defined(ASSISTANT_BOARD_P4_JD9365_4C)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-4c";
#elif defined(ASSISTANT_BOARD_P4_JD9365_8)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-8";
#else
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-10.1";
#endif
#else
#if defined(ASSISTANT_BOARD_P4_JD9365_34C)
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-3.4c";
#elif defined(ASSISTANT_BOARD_P4_JD9365_4C)
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-4c";
#elif defined(ASSISTANT_BOARD_P4_JD9365_8)
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-8";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-10.1";
#endif
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
// Small square rasters use a circular-safe content area pending bezel acceptance.
inline constexpr bool round = height == width, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_ST7703)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-4b";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-4b";
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_HX8394)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-5";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-5";
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_ST7701)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-4.3";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-4.3";
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_ILI9881C)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-7";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-7";
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_7B)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-7b";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-7b";
#endif
inline constexpr int width = waveshare_p4_mipi::panel.width, height = waveshare_p4_mipi::panel.height;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_P4_FUNCTION)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "espressif-esp32-p4-function-ev-board";
#else
inline constexpr const char *name = "espressif-esp32-p4x-function-ev-board";
#endif
inline constexpr int width = 1024, height = 600;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_SENSECAP)
inline constexpr const char *name = "sensecap-watcher";
inline constexpr int width = 412, height = 412;
inline constexpr bool round = true, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_MATRIX)
inline constexpr const char *name = "waveshare-esp32-s3-rgb-matrix";
inline constexpr int width = 64, height = 64;
inline constexpr bool round = false, separate_microphone = true;
// No application button is defined by the fixed owning board; GPIO0 is not
// assumed accessible. Talk and reminder operations use the voice/console path.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_WS_RLCD)
inline constexpr const char *name = "waveshare-esp32-s3-rlcd-4.2";
inline constexpr int width = 400, height = 300;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_EDA_BEAR)
inline constexpr const char *name = "lceda-course-examples-eda-super-bear";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = true;
// The only actual application key opens the phone view; talk uses wake.
// GPIO14's unused TOUCH macro conflicts with the right-foot servo pin.
inline constexpr gpio_num_t listen_button = GPIO_NUM_38;
#elif defined(ASSISTANT_BOARD_DF_AI_CAM)
inline constexpr const char *name = "dfrobot-df-s3-ai-cam";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = true;
// Reserve the single BOOT key for reopening the phone view; talk uses wake.
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_ATOM_M12_BASE)
inline constexpr const char *name = "m5stack-atoms3r-cam-m12-echo-base";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = false;
// The owning board has no application key; GPIO41 in its config is unused.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_S31_CORE)
inline constexpr const char *name = "espressif-esp32-s31-function-coreboard-1";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = false;
// Reserve BOOT61 for reopening the phone view; talk uses the wake phrase.
inline constexpr gpio_num_t listen_button = GPIO_NUM_61;
#elif defined(ASSISTANT_BOARD_ATOM_ECHOS3R)
inline constexpr const char *name = "m5stack-atom-echos3r";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = false;
// GPIO41 is reserved for the local portal;voice wake remains the talk input.
inline constexpr gpio_num_t listen_button = GPIO_NUM_41;
#elif defined(ASSISTANT_BOARD_DOIT)
inline constexpr const char *name = "doit-s3-aibox";
inline constexpr int width = 0, height = 0;
inline constexpr bool round = false, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_10;
#elif defined(ASSISTANT_BOARD_WS_EPAPER397)
inline constexpr const char *name = "waveshare-esp32-s3-epaper-3.97";
inline constexpr int width = 800, height = 480;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_ZECTRIX)
inline constexpr const char *name = "zectrix-s3-epaper-4.2";
inline constexpr int width = 400, height = 300;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_WS_EPAPER)
inline constexpr const char *name = "waveshare-esp32-s3-epaper-1.54-v2";
inline constexpr int width = 200, height = 200;
inline constexpr bool round = false, separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_MAZA)
#if defined(ASSISTANT_BOARD_MAZA_S31)
inline constexpr const char *name = "maza-ai-esp32-s31";
inline constexpr gpio_num_t listen_button = GPIO_NUM_1;
#else
inline constexpr const char *name = "maza-ai-esp32-s3";
inline constexpr gpio_num_t listen_button = GPIO_NUM_9;
#endif
inline constexpr int width = 296, height = 128;
inline constexpr bool round = false, separate_microphone = false;
#elif defined(ASSISTANT_BOARD_OLED)
#if defined(ASSISTANT_BOARD_NT26)
inline constexpr const char *name = "bread-compact-nt26";
#elif defined(ASSISTANT_BOARD_EDA_ROBOT)
inline constexpr const char *name = "lceda-course-examples-eda-robot-pro";
#elif defined(ASSISTANT_BOARD_WK)
inline constexpr const char *name = "wk-esp32s3-dev";
#elif defined(ASSISTANT_BOARD_KEVIN_BOX2)
inline constexpr const char *name = "kevin-box-2";
#elif defined(ASSISTANT_BOARD_HU087)
inline constexpr const char *name = "hu-087";
#elif defined(ASSISTANT_BOARD_OLED_CUBE)
inline constexpr const char *name = "nologo-xingzhi-cube-0.96oled-wifi";
#elif defined(ASSISTANT_BOARD_OLED_64)
inline constexpr const char *name = "bread-compact-wifi-128x64";
#else
inline constexpr const char *name = "bread-compact-wifi";
#endif
inline constexpr int width = 128;
#if defined(ASSISTANT_BOARD_OLED_64)
inline constexpr int height = 64;
#else
inline constexpr int height = 32;
#endif
inline constexpr bool round = false, separate_microphone = true;
#if defined(ASSISTANT_BOARD_EDA_ROBOT)
inline constexpr gpio_num_t listen_button = GPIO_NUM_14;
#elif defined(ASSISTANT_BOARD_HU087)
inline constexpr gpio_num_t listen_button = GPIO_NUM_18;
#else
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#endif
#elif defined(ASSISTANT_BOARD_GUITION)
#if defined(ASSISTANT_BOARD_GUITION_EXT_AMP)
inline constexpr const char *name = "guition-jc4827w543-ext-amp";
#else
inline constexpr const char *name = "guition-jc4827w543";
#endif
inline constexpr int width = 480, height = 272;
inline constexpr bool round = false, separate_microphone = true;
// Use the accessible screen input. BOOT0 shares the panel TE net and remains
// unused by this entry until physical acceptance of that shared line.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_COMMUNITY_TFT)
#if defined(ASSISTANT_BOARD_P4_SPI35)
#if defined(ASSISTANT_BOARD_P4_REV_LT3)
inline constexpr const char *name = "waveshare-esp32-p4-wifi6-touch-lcd-3.5";
#else
inline constexpr const char *name = "waveshare-esp32-p4x-wifi6-touch-lcd-3.5";
#endif
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_ELECTRON)
inline constexpr const char *name = "electron-bot";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_K10)
inline constexpr const char *name = "dfrobot-df-k10";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_MVSRLORA)
inline constexpr const char *name = "lilygo-t-display-s3-pro-mvsrlora";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_TCAMERA)
#if defined(ASSISTANT_BOARD_TCAMERA_V12)
inline constexpr const char *name = "lilygo-t-cameraplus-s3-v1.2";
#else
inline constexpr const char *name = "lilygo-t-cameraplus-s3";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_VOCAT)
inline constexpr const char *name = "espressif-esp-vocat";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_LCD_EV)
#if defined(ASSISTANT_BOARD_LCD_EV_14)
inline constexpr const char *name = "espressif-esp-s3-lcd-ev-board-1p4";
#elif defined(ASSISTANT_BOARD_LCD_EV_2)
inline constexpr const char *name = "espressif-esp-s3-lcd-ev-board-2";
#else
inline constexpr const char *name = "espressif-esp-s3-lcd-ev-board-1p5";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_STOPWATCH)
inline constexpr const char *name = "m5stack-stopwatch";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_SPARKBOT)
inline constexpr const char *name = "espressif-esp-sparkbot";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_CARDPUTER_ADV)
inline constexpr const char *name = "m5stack-cardputer-adv";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_ATOM_S3_BASE)
inline constexpr const char *name = "m5stack-atoms3-echo-base";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_WS143C)
inline constexpr const char *name = "waveshare-esp32-s3-touch-amoled-1.43c";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_WS349)
#if defined(ASSISTANT_BOARD_WS349_V2)
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-3.49-v2";
#else
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-3.49";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_WS35)
#if defined(ASSISTANT_BOARD_WS35B)
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-3.5b";
#else
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-3.5";
#endif
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_WS4B)
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-4b";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
#if defined(ASSISTANT_BOARD_WS_RGB_7C)
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-7c";
#else
inline constexpr const char *name = "waveshare-esp32-s3-touch-lcd-4.3c";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_KEVIN_RGB)
inline constexpr const char *name = "kevin-yuying-313lcd";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_LCDWIKI)
inline constexpr const char *name = "lcdwiki-es3c35p";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_OTTO)
inline constexpr const char *name = "otto-robot";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_EDA_TV)
inline constexpr const char *name = "lceda-course-examples-eda-tv-pro";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_WS_CAM)
#if defined(ASSISTANT_BOARD_WS_CAM_183)
inline constexpr const char *name = "waveshare-esp32-s3-cam-1.83";
#elif defined(ASSISTANT_BOARD_WS_CAM_28)
inline constexpr const char *name = "waveshare-esp32-s3-cam-2.8";
#elif defined(ASSISTANT_BOARD_WS_CAM_35)
inline constexpr const char *name = "waveshare-esp32-s3-cam-3.5";
#else
inline constexpr const char *name = "waveshare-esp32-s3-cam-2";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_BREAD_S3_CAM)
inline constexpr const char *name = "bread-compact-wifi-s3cam";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
inline constexpr const char *name = "zhengchen-cam";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_KORVO2)
#if defined(ASSISTANT_BOARD_RNDIS)
inline constexpr const char *name = "espressif-esp32s3-korvo2-v3-rndis";
#else
inline constexpr const char *name = "espressif-esp32s3-korvo2-v3";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_QUANDONG)
inline constexpr const char *name = "quandong-s3-dev";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_FREENOVE)
inline constexpr const char *name = "freenove-esp32s3-display-2.8-lcd";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_CHD_EYE)
inline constexpr const char *name = "chd-esp32s3-eye";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_STICK_S3)
inline constexpr const char *name = "m5stack-stick-s3";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_ECHO_PYRAMID)
inline constexpr const char *name = "m5stack-atoms3r-echo-pyramid";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_ATOM_S3R_BASE)
inline constexpr const char *name = "m5stack-atoms3r-echo-base";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_CHD)
#if defined(ASSISTANT_BOARD_CHD_LCD35)
inline constexpr const char *name = "chd-esp32s3-box-lcd35";
#else
inline constexpr const char *name = "chd-esp32s3-box";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_LABPLUS)
#if defined(ASSISTANT_BOARD_LEDONG)
inline constexpr const char *name = "labplus-ledong-v2";
#else
inline constexpr const char *name = "labplus-mpython-v3";
#endif
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_MIXGO)
inline constexpr const char *name = "mixgo-nova";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_MAGICLICK)
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
inline constexpr const char *name = "magiclick-2p5";
#else
inline constexpr const char *name = "magiclick-2p4";
#endif
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_MINSI)
inline constexpr const char *name = "minsi-k08-dual";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_AIVOX3)
inline constexpr const char *name = "nulllab-ai-vox-v3";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_WS_AUDIO)
inline constexpr const char *name = "waveshare-audio-board";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_AIPI)
inline constexpr const char *name = "xorigin-aipi-lite";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_ATK_BOX3)
inline constexpr const char *name = "alientek-atk-dnesp32s3-box3";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_ATK_BOX2)
inline constexpr const char *name = "alientek-atk-dnesp32s3-box2-wifi";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_ATK_BOX)
inline constexpr const char *name = "alientek-atk-dnesp32s3-box";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_ATK_BOX0)
inline constexpr const char *name = "alientek-atk-dnesp32s3-box0";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_ATK_DEV)
inline constexpr const char *name = "alientek-atk-dnesp32s3";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_BIGSMART)
inline constexpr const char *name = "rymcu-bigsmart";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_LICHUANG)
inline constexpr const char *name = "lckfb-lichuang-dev";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
inline constexpr const char *name = "jiuchuan-s3";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_DUCHATX)
inline constexpr const char *name = "du-chatx";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_KEVIN_SPV4)
inline constexpr const char *name = "kevin-sp-v4-dev";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_YUNLIAO)
inline constexpr const char *name = "yunliao-s3";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_LC_TFT)
inline constexpr const char *name = "lc-s3-wifi-1.54tft";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_XINGZHI_ABS)
inline constexpr const char *name = "nologo-xingzhi-abs-2.0";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_XINGZHI_METAL)
inline constexpr const char *name = "nologo-xingzhi-metal-1.54-wifi";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_SPOTPEAR_BOX)
inline constexpr const char *name = "spotpear-sp-esp32-s3-1.28-box";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_SPOTPEAR_MUMA)
inline constexpr const char *name = "spotpear-sp-esp32-s3-1.54-muma";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_MOVECALL_MOJI)
inline constexpr const char *name = "movecall-moji-esp32s3";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_MOVECALL_CUICAN)
inline constexpr const char *name = "movecall-cuican-esp32s3";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_GENJUTECH)
inline constexpr const char *name = "genjutech-s3-1.54tft";
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_XINGZHI_085)
inline constexpr const char *name = "nologo-xingzhi-cube-0.85tft-wifi";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_XINGZHI_TFT)
inline constexpr const char *name = "nologo-xingzhi-cube-1.54tft-wifi";
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_BREAD_TFT)
inline constexpr const char *name = "bread-compact-wifi-lcd";
inline constexpr bool separate_microphone = true;
#else
inline constexpr const char *name = "zhengchen-1.54tft-wifi";
inline constexpr bool separate_microphone = true;
#endif
inline constexpr int width = spi_board::width, height = spi_board::height;
#if defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_WS143C) || defined(ASSISTANT_BOARD_STOPWATCH) || defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_ELECTRON)
inline constexpr bool round = true;
#else
inline constexpr bool round = false;
#endif
#if defined(ASSISTANT_BOARD_P4_SPI35)
inline constexpr gpio_num_t listen_button = GPIO_NUM_35;
#elif defined(ASSISTANT_BOARD_K10)
// GPIO0isI2SBCLK;only TCA9555keys may requesttalk.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_STOPWATCH)
inline constexpr gpio_num_t listen_button = GPIO_NUM_2;
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
// GPIO0 is RGB DATA6; do not configure it as a BOOT input.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_KORVO2)
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_STICK_S3)
inline constexpr gpio_num_t listen_button = GPIO_NUM_11;
#elif defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)
inline constexpr gpio_num_t listen_button = GPIO_NUM_41;
#elif defined(ASSISTANT_BOARD_MAGICLICK)
inline constexpr gpio_num_t listen_button = GPIO_NUM_21;
#elif defined(ASSISTANT_BOARD_AIPI)
inline constexpr gpio_num_t listen_button = GPIO_NUM_42;
#elif defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_ATK_BOX3)
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_ATK_BOX0)
inline constexpr gpio_num_t listen_button = GPIO_NUM_4;
#elif defined(ASSISTANT_BOARD_XINGZHI_METAL)
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_YUNLIAO)
inline constexpr gpio_num_t listen_button = GPIO_NUM_2;
#else
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#endif
#elif defined(ASSISTANT_BOARD_TAIJI)
#if defined(ASSISTANT_BOARD_TAIJI_PDM)
inline constexpr const char *name = "taiji-pi-s3-pdm";
#else
inline constexpr const char *name = "taiji-pi-s3";
#endif
inline constexpr int width = 360, height = 360;
inline constexpr bool round = true, separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_TCIRCLE)
#if defined(ASSISTANT_BOARD_TCIRCLE_V11)
inline constexpr const char *name = "lilygo-t-circle-s3-v11";
#else
inline constexpr const char *name = "lilygo-t-circle-s3";
#endif
inline constexpr int width = 160;
inline constexpr int height = 160;
inline constexpr bool round = true;
inline constexpr bool separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_LCD_146)
inline constexpr const char *name = "waveshare-lcd-1.46";
inline constexpr int width = 412, height = 412;
inline constexpr bool round = true;
inline constexpr bool separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_LCD_185_FAMILY)
#if defined(ASSISTANT_BOARD_LCD_185B)
inline constexpr const char *name = "waveshare-lcd-1.85b";
#elif defined(ASSISTANT_BOARD_LCD_185C)
inline constexpr const char *name = "waveshare-lcd-1.85c";
#else
inline constexpr const char *name = "waveshare-lcd-1.85";
#endif
inline constexpr int width = 360, height = 360;
inline constexpr bool round = true;
inline constexpr bool separate_microphone = true;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_WAVESHARE_SPI)
#if defined(ASSISTANT_BOARD_LCD_085)
inline constexpr const char *name = "waveshare-lcd-0.85";
// GPIO0 is volume down on this board. GPIO4 (PLUS) is used for manual input;
// PWR5 retains its independent long-hold battery-latch behavior.
inline constexpr gpio_num_t listen_button = GPIO_NUM_4;
#elif defined(ASSISTANT_BOARD_LCD_154)
inline constexpr const char *name = "waveshare-lcd-1.54";
// GPIO0 is volume down and GPIO5 is the battery power key. Use touch to talk.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#else
inline constexpr const char *name = "waveshare-lcd-1.83";
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#endif
inline constexpr int width = spi_board::width;
inline constexpr int height = spi_board::height;
inline constexpr bool round = false;
inline constexpr bool separate_microphone = true;
#elif defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_AMOLED_216)
#if defined(ASSISTANT_BOARD_AMOLED_132)
inline constexpr const char *name = "waveshare-amoled-1.32";
inline constexpr int width = 466;
inline constexpr int height = 466;
inline constexpr bool round = true;
inline constexpr bool separate_microphone = false;
#elif defined(ASSISTANT_BOARD_AMOLED_206)
inline constexpr const char *name = "waveshare-amoled-2.06";
inline constexpr int width = 410;
inline constexpr int height = 502;
inline constexpr bool round = false;
inline constexpr bool separate_microphone = true;
#else
inline constexpr const char *name = "waveshare-amoled-2.16";
inline constexpr int width = 480;
inline constexpr int height = 480;
inline constexpr bool round = false;
inline constexpr bool separate_microphone = true;
#endif
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE)
#if defined(ASSISTANT_BOARD_BOX)
inline constexpr const char *name = "esp32-s3-box";
#elif defined(ASSISTANT_BOARD_BOXLITE)
inline constexpr const char *name = "esp32-s3-box-lite";
#else
inline constexpr const char *name = "esp32-s3-box-3";
#endif
inline constexpr int width = 320;
inline constexpr int height = 240;
inline constexpr bool round = false;
inline constexpr bool separate_microphone = true;
inline constexpr gpio_num_t listen_button = BSP_BUTTON_CONFIG_IO;
#elif defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)
#if defined(ASSISTANT_BOARD_AMOLED_175C)
inline constexpr const char *name = "waveshare-amoled-1.75c";
#else
inline constexpr const char *name = "waveshare-amoled-1.75";
#endif
inline constexpr int width = 466;
inline constexpr int height = 466;
inline constexpr bool round = true;
inline constexpr bool separate_microphone = true;
// The vendor pinout reserves GPIO0 as BOOT; do not reuse the AMOLED reset pin.
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_CORES3)
inline constexpr const char *name = "m5stack-core-s3";
inline constexpr int width = 320;
inline constexpr int height = 240;
inline constexpr bool round = false;
inline constexpr bool separate_microphone = true;
// GPIO0 is the audio MCLK. CoreS3 has no application GPIO button; use touch.
inline constexpr gpio_num_t listen_button = GPIO_NUM_NC;
#else
#if defined(ASSISTANT_BOARD_SH8601)
inline constexpr const char *name = "waveshare-amoled-1.8-sh8601";
#elif defined(ASSISTANT_BOARD_AMOLED_18_V2)
inline constexpr const char *name = "waveshare-amoled-1.8-v2";
#else
// Historical entry name; the fixed 2.0.3 BSP drives CO5300, not SH8601.
inline constexpr const char *name = "waveshare-amoled-1.8";
#endif
inline constexpr int width = 368;
inline constexpr int height = 448;
inline constexpr bool round = false;
inline constexpr bool separate_microphone = false;
inline constexpr gpio_num_t listen_button = GPIO_NUM_0;
#endif

#if defined(ASSISTANT_BOARD_XINGZHI_METAL) || defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || defined(ASSISTANT_BOARD_KORVO2) || defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_SENSECAP)
// Board-specific key readers are not an LVGL screen pointer or a GPIO key.
inline constexpr bool touch_listen = false;
#else
inline constexpr bool touch_listen = listen_button == GPIO_NUM_NC;
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_SPOT_C5)
inline constexpr bool listen_active_high = true;
#else
inline constexpr bool listen_active_high = false;
#endif
inline ManualInputKey read_manual_key() {
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
    return sensair_read_keys();
#elif defined(ASSISTANT_BOARD_SPOT_C5)
    const bool talk = gpio_get_level(listen_button), portal = !gpio_get_level(GPIO_NUM_28);
    static bool released_once = false;
    if (!talk && !portal) { released_once = true; return ManualInputKey::None; }
    if (!released_once || portal) return ManualInputKey::Invalid;
    return ManualInputKey::Listen;
#elif defined(ASSISTANT_BOARD_C5_LCD169) || defined(ASSISTANT_BOARD_MOJI2_C5)
    const bool pressed = !gpio_get_level(listen_button);
    static bool released_once = false;
    if (!pressed) { released_once = true; return ManualInputKey::None; }
    return released_once ? ManualInputKey::Listen : ManualInputKey::Invalid;
#elif defined(ASSISTANT_BOARD_S31_KORVO)
    return s31_korvo_read_keys();
#elif defined(ASSISTANT_BOARD_SENSECAP)
    return sensecap_read_key();
#elif defined(ASSISTANT_BOARD_P4_SPI35) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_MOSAICO)
    const bool pressed = !gpio_get_level(listen_button);
    static bool released_once = false;
    if (!pressed) { released_once = true; return ManualInputKey::None; }
    return released_once ? ManualInputKey::Listen : ManualInputKey::Invalid;
#elif defined(ASSISTANT_BOARD_K10)
    return k10_read_keys();
#else
#if defined(ASSISTANT_BOARD_STOPWATCH)
    return stopwatch_read_keys();
#elif defined(ASSISTANT_BOARD_CARDPUTER_ADV)
    return cardputer_adv_read_keys();
#elif defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_DF_AI_CAM) || defined(ASSISTANT_BOARD_EDA_BEAR) || defined(ASSISTANT_BOARD_S31_CORE)
    return ManualInputKey::None;
#elif defined(ASSISTANT_BOARD_KORVO2)
    return korvo2_read_keys();
#elif defined(ASSISTANT_BOARD_FREENOVE)
    return freenove_keys_read();
#elif defined(ASSISTANT_BOARD_ATK_BOX2)
    return atk_box2_read_keys();
#elif defined(ASSISTANT_BOARD_ATK_BOX3)
    return atk_box3_read_keys();
#elif defined(ASSISTANT_BOARD_ATK_BOX0)
    const bool listen = gpio_get_level(listen_button);
    const bool volume_up = !gpio_get_level(GPIO_NUM_0), volume_down = !gpio_get_level(GPIO_NUM_3);
    const int pressed_count = listen + volume_up + volume_down;
    static bool released_once = false;
    if (!pressed_count) { released_once = true; return ManualInputKey::None; }
    if (!released_once || pressed_count > 1) return ManualInputKey::Invalid;
    if (listen) return ManualInputKey::Listen;
    return volume_up ? ManualInputKey::VolumeUp : ManualInputKey::VolumeDown;
#elif defined(ASSISTANT_BOARD_DOIT)
    const bool listen = !gpio_get_level(listen_button);
    const bool volume_up = !gpio_get_level(GPIO_NUM_15), volume_down = !gpio_get_level(GPIO_NUM_9);
    const int pressed_count = listen + volume_up + volume_down;
    static bool released_once = false;
    if (!pressed_count) { released_once = true; return ManualInputKey::None; }
    if (!released_once || pressed_count > 1) return ManualInputKey::Invalid;
    if (listen) return ManualInputKey::Listen;
    return volume_up ? ManualInputKey::VolumeUp : ManualInputKey::VolumeDown;
#elif defined(ASSISTANT_BOARD_MAZA)
#if defined(ASSISTANT_BOARD_MAZA_S31)
    const bool listen = !gpio_get_level(listen_button), touch = !gpio_get_level(GPIO_NUM_12);
    const bool volume_up = !gpio_get_level(GPIO_NUM_4), volume_down = !gpio_get_level(GPIO_NUM_61);
#else
    const bool listen = !gpio_get_level(listen_button), touch = !gpio_get_level(GPIO_NUM_10);
    const bool volume_up = !gpio_get_level(GPIO_NUM_8), volume_down = !gpio_get_level(GPIO_NUM_0);
#endif
    const int pressed_count = listen + touch + volume_up + volume_down;
    static bool released_once = false;
    if (!pressed_count) { released_once = true; return ManualInputKey::None; }
    if (!released_once || pressed_count > 1) return ManualInputKey::Invalid;
    if (listen || touch) return ManualInputKey::Listen;
    return volume_up ? ManualInputKey::VolumeUp : ManualInputKey::VolumeDown;
#elif defined(ASSISTANT_BOARD_XINGZHI_METAL)
    return metal_keys_read();
#elif defined(ASSISTANT_BOARD_HU087) || defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_CHD) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_BREAD_S3_CAM) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_EDA_TV) || defined(ASSISTANT_BOARD_EDA_ROBOT) || defined(ASSISTANT_BOARD_OTTO) || defined(ASSISTANT_BOARD_LCDWIKI) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_WS4B) || defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C) || defined(ASSISTANT_BOARD_ATOM_S3_BASE) || defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_SPARKBOT) || defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_ELECTRON) || defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_WS_EPAPER397) || defined(ASSISTANT_BOARD_WS_RLCD)
    const bool pressed = !gpio_get_level(listen_button);
    static bool released_once = false;
    if (!pressed) { released_once = true; return ManualInputKey::None; }
    return released_once ? ManualInputKey::Listen : ManualInputKey::Invalid;
#elif defined(ASSISTANT_BOARD_GENJUTECH) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_MINSI) || defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    const bool listen = !gpio_get_level(listen_button);
#if defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    const bool volume_up = !gpio_get_level(GPIO_NUM_3);
    const bool volume_down = !gpio_get_level(GPIO_NUM_46);
    const bool touch_key = false;
#elif defined(ASSISTANT_BOARD_MAGICLICK)
    const bool volume_up = !gpio_get_level(GPIO_NUM_47);
    const bool volume_down = !gpio_get_level(GPIO_NUM_0);
    const bool touch_key = false;
#elif defined(ASSISTANT_BOARD_MINSI)
    const bool volume_up = !gpio_get_level(GPIO_NUM_47);
    const bool volume_down = !gpio_get_level(GPIO_NUM_46);
    const bool touch_key = false;
#elif defined(ASSISTANT_BOARD_AIVOX3)
    const bool volume_up = !gpio_get_level(GPIO_NUM_46);
    const bool volume_down = !gpio_get_level(GPIO_NUM_45);
    const bool touch_key = false;
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
    const bool volume_up = !gpio_get_level(GPIO_NUM_6);
    const bool volume_down = !gpio_get_level(GPIO_NUM_7);
    const bool touch_key = false;
#elif defined(ASSISTANT_BOARD_OLED)
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
    const bool volume_up = !gpio_get_level(GPIO_NUM_1);
    const bool volume_down = !gpio_get_level(GPIO_NUM_2);
#elif defined(ASSISTANT_BOARD_WK)
    const bool volume_up = !gpio_get_level(GPIO_NUM_38);
    const bool volume_down = !gpio_get_level(GPIO_NUM_39);
#else
    const bool volume_up = !gpio_get_level(GPIO_NUM_40);
    const bool volume_down = !gpio_get_level(GPIO_NUM_39);
#endif
#if defined(ASSISTANT_BOARD_OLED_CUBE) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_WK)
    const bool touch_key = false;
#else
    const bool touch_key = !gpio_get_level(GPIO_NUM_47);
#endif
#else
    const bool volume_up = !gpio_get_level(GPIO_NUM_42);
    const bool volume_down = !gpio_get_level(GPIO_NUM_41);
    const bool touch_key = false;
#endif
    const int pressed_count = listen + touch_key + volume_up + volume_down;
    static bool released_once = false;
    if (!pressed_count) { released_once = true; return ManualInputKey::None; }
    if (!released_once || pressed_count > 1) return ManualInputKey::Invalid;
    if (listen || touch_key) return ManualInputKey::Listen;
    return volume_up ? ManualInputKey::VolumeUp : ManualInputKey::VolumeDown;
#else
    if constexpr (listen_button == GPIO_NUM_NC) return ManualInputKey::None;
    return gpio_get_level(listen_button) ? ManualInputKey::None : ManualInputKey::Listen;
#endif
#endif
}
inline constexpr bool has_touch = BSP_CAPS_TOUCH != 0;
#if defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_MAZA) || defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_WS_EPAPER397) || defined(ASSISTANT_BOARD_WS_RLCD)
inline constexpr bool monochrome = true;
#else
inline constexpr bool monochrome = false;
#endif
#if defined(ASSISTANT_BOARD_MAZA) || defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_WS_EPAPER397)
inline constexpr bool electronic_paper = true;
#else
inline constexpr bool electronic_paper = false;
#endif
#if defined(ASSISTANT_BOARD_WS_RLCD)
inline constexpr bool reflective_display = true;
#else
inline constexpr bool reflective_display = electronic_paper;
#endif
#if defined(ASSISTANT_BOARD_C5_LCD169) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_CHD) || defined(ASSISTANT_BOARD_LCDWIKI) || defined(ASSISTANT_BOARD_WS4B) || defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C) || defined(ASSISTANT_BOARD_STOPWATCH) || defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_MOSAICO) || defined(ASSISTANT_BOARD_S31_KORVO)
// BOOT remains usable for card actions on both touch and non-touch revisions.
inline constexpr bool hardware_card_input = true;
#else
#if defined(ASSISTANT_BOARD_MATRIX)
inline constexpr bool hardware_card_input = false;
#else
inline constexpr bool hardware_card_input = !has_touch;
#endif
#endif
#if defined(ASSISTANT_BOARD_BOXLITE) || defined(ASSISTANT_BOARD_SPOT_C5) || defined(ASSISTANT_BOARD_SENSAIR_C5)
inline constexpr uint32_t microphone_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_S31_KORVO)
inline constexpr uint32_t microphone_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_TCAMERA_V12) || defined(ASSISTANT_BOARD_MVSRLORA)
// MP34DT05-A requires1.2..3.25MHz PDMCLK;32kHz*64=2.048MHz.
inline constexpr uint32_t microphone_sample_rate = 32000;
#elif defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_ELECTRON) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_MATRIX)
inline constexpr uint32_t microphone_sample_rate = 16000;
#elif (defined(ASSISTANT_BOARD_LCD_EV) && !defined(ASSISTANT_BOARD_LCD_EV_2)) || defined(ASSISTANT_BOARD_SPARKBOT)
inline constexpr uint32_t microphone_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_TCIRCLE_V11)
// MP34DT05-A needs a PDM clock above 1.2MHz: 32kHz * 64 = 2.048MHz.
inline constexpr uint32_t microphone_sample_rate = 32000;
#elif defined(ASSISTANT_BOARD_KEVIN_BOX2)
inline constexpr uint32_t microphone_sample_rate = 24000;
#elif defined(ASSISTANT_BOARD_DOIT) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_DF_AI_CAM) || defined(ASSISTANT_BOARD_EDA_BEAR) || defined(ASSISTANT_BOARD_KEVIN_RGB)
inline constexpr uint32_t microphone_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_GENJUTECH) || defined(ASSISTANT_BOARD_GUITION)
inline constexpr uint32_t microphone_sample_rate = 16000;
#else
inline constexpr uint32_t microphone_sample_rate = 24000;
#endif
#if defined(ASSISTANT_BOARD_BOXLITE) || defined(ASSISTANT_BOARD_SPOT_C5)
inline constexpr uint32_t speaker_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_S31_KORVO)
inline constexpr uint32_t speaker_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_ZECTRIX) || defined(ASSISTANT_BOARD_MATRIX)
// Manufacturerstandardduplex16kHz matchesES7243E and directdigitalspeaker.
inline constexpr uint32_t speaker_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_TCIRCLE) || defined(ASSISTANT_BOARD_MINSI) || defined(ASSISTANT_BOARD_GUITION_EXT_AMP) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_ELECTRON)
// MAX98357A/B datasheetp19excludes24kHz;use16kHz and the common networkFIR.
inline constexpr uint32_t speaker_sample_rate = 16000;
#elif (defined(ASSISTANT_BOARD_LCD_EV) && !defined(ASSISTANT_BOARD_LCD_EV_2)) || defined(ASSISTANT_BOARD_SPARKBOT) || defined(ASSISTANT_BOARD_TCAMERA)
inline constexpr uint32_t speaker_sample_rate = 16000;
#elif defined(ASSISTANT_BOARD_OTTO)
// Runtime hardware detection sets this once before audio initialization.
inline uint32_t speaker_sample_rate = 0;
#elif defined(ASSISTANT_BOARD_GENJUTECH) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB)
inline constexpr uint32_t speaker_sample_rate = 16000;
#else
inline constexpr uint32_t speaker_sample_rate = 24000;
#endif
#if defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_SPOT_C5)
inline constexpr bool microphone_gain = true;
#elif defined(ASSISTANT_BOARD_TCIRCLE) || defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_TAIJI) || defined(ASSISTANT_BOARD_HEADLESS) || defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_SENSAIR_C5)
inline constexpr bool microphone_gain = false;
#else
inline constexpr bool microphone_gain = true;
#endif
#if defined(ASSISTANT_BOARD_INTERNAL_RAM)
inline constexpr bool manual_audio_only = true;
inline constexpr uint32_t working_memory_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#else
inline constexpr bool manual_audio_only = false;
inline constexpr uint32_t working_memory_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
#if defined(ASSISTANT_BOARD_MATRIX)
inline constexpr bool text_only = true;
#else
inline constexpr bool text_only = monochrome || manual_audio_only;
#endif
inline constexpr bool compact_display = width <= 160;

// Local display wrappers return bool from the selected LVGL port.
inline bool display_lock(uint32_t timeout_ms) {
#if defined(ASSISTANT_BOARD_HEADLESS)
    (void)timeout_ms;
    return false;
#else
    return bsp_display_lock(timeout_ms);
#endif
}
}
