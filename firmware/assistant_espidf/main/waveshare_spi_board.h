#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#if defined(ASSISTANT_BOARD_LCD_085)
#define BSP_CAPS_TOUCH 0
#else
#define BSP_CAPS_TOUCH 1
#endif
#if defined(ASSISTANT_BOARD_LCD_183)
#define BSP_CAPS_SDCARD 1
#define BSP_SD_MOUNT_POINT "/sdcard"
esp_err_t bsp_sdcard_mount();
#else
#define BSP_CAPS_SDCARD 0
#endif

// Electrical facts from the pinned v2.5.2 board config; drivers are the existing
// ESP-IDF ST7789, esp_lcd_touch_cst816s and esp_codec_dev implementations.
namespace spi_board {
#if defined(ASSISTANT_BOARD_LCD_085)
inline constexpr int width = 128;
#elif defined(ASSISTANT_BOARD_LCD_185B) || defined(ASSISTANT_BOARD_LCD_185C)
inline constexpr int width = 360;
#else
inline constexpr int width = 240;
#endif
#if defined(ASSISTANT_BOARD_LCD_154) || defined(ASSISTANT_BOARD_LCD_085)
#if defined(ASSISTANT_BOARD_LCD_085)
inline constexpr int height = 128;
#else
inline constexpr int height = 240;
#endif
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_42, i2c_scl = GPIO_NUM_41;
inline constexpr gpio_num_t mclk = GPIO_NUM_8, bclk = GPIO_NUM_9, word_select = GPIO_NUM_10;
inline constexpr gpio_num_t audio_output = GPIO_NUM_12, audio_input = GPIO_NUM_11, amplifier = GPIO_NUM_7;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_21, lcd_clock = GPIO_NUM_38, lcd_mosi = GPIO_NUM_39;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_45, lcd_reset = GPIO_NUM_40, backlight = GPIO_NUM_46;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_47, touch_interrupt = GPIO_NUM_48;
inline constexpr int spi_mode = 3, spi_clock_hz = 40000000, touch_clock_hz = 100000;
#elif defined(ASSISTANT_BOARD_LCD_183)
inline constexpr int height = 284;
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_15, i2c_scl = GPIO_NUM_14;
inline constexpr gpio_num_t mclk = GPIO_NUM_16, bclk = GPIO_NUM_9, word_select = GPIO_NUM_45;
inline constexpr gpio_num_t audio_output = GPIO_NUM_8, audio_input = GPIO_NUM_10, amplifier = GPIO_NUM_46;
inline constexpr gpio_num_t lcd_cs = GPIO_NUM_5, lcd_clock = GPIO_NUM_6, lcd_mosi = GPIO_NUM_7;
inline constexpr gpio_num_t lcd_dc = GPIO_NUM_4, lcd_reset = GPIO_NUM_38, backlight = GPIO_NUM_40;
inline constexpr gpio_num_t touch_reset = GPIO_NUM_39, touch_interrupt = GPIO_NUM_13;
// The upstream config macro says mode3, but the actual board class uses0/24MHz.
inline constexpr int spi_mode = 0, spi_clock_hz = 24000000, touch_clock_hz = 400000;
#elif defined(ASSISTANT_BOARD_LCD_185B) || defined(ASSISTANT_BOARD_LCD_185C)
// These audio constants are shared by the codec implementation; their QSPI
// displays and reset/touch pins are initialized by waveshare_qspi_display.cpp.
inline constexpr int height = 360;
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_11, i2c_scl = GPIO_NUM_10;
inline constexpr gpio_num_t mclk = GPIO_NUM_2, bclk = GPIO_NUM_48, word_select = GPIO_NUM_38;
inline constexpr gpio_num_t audio_output = GPIO_NUM_47, audio_input = GPIO_NUM_39;
#if defined(ASSISTANT_BOARD_LCD_185B)
inline constexpr gpio_num_t amplifier = GPIO_NUM_9;
#else
inline constexpr gpio_num_t amplifier = GPIO_NUM_15;
#endif
#else
#error "No implemented Waveshare SPI board selected"
#endif
}

esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
