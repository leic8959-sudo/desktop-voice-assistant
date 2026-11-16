#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
#include "esp_codec_dev.h"
namespace kevin_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_40, bclk = GPIO_NUM_38, word_select = GPIO_NUM_47;
inline constexpr gpio_num_t audio_input = GPIO_NUM_39, audio_output = GPIO_NUM_48, amplifier = GPIO_NUM_9;
}
esp_err_t bsp_codec_i2c_init();
i2c_master_bus_handle_t bsp_codec_i2c_get_handle();
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
#else
#include "digital_audio.h"
#endif

#define BSP_CAPS_TOUCH 0
#define BSP_CAPS_SDCARD 0

esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
