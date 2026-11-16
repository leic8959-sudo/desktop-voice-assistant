#pragma once

#if defined(ASSISTANT_BOARD_MOJI2_C5)
#include "moji2_c5_board.h"
#elif defined(ASSISTANT_BOARD_VOCAT)
#include "tft_board.h"
#elif defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TAIJI)
#include "digital_audio.h"
#else
#include "waveshare_spi_board.h"
#endif
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"

#ifndef BSP_CAPS_TOUCH
#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0
#endif

// Local QSPI glue with board-specific display, touch, reset and audio selection.
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
