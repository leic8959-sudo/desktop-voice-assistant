#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0
#define BSP_POWER_AMP_IO GPIO_NUM_45

namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_54, bclk = GPIO_NUM_37, word_select = GPIO_NUM_49;
inline constexpr gpio_num_t audio_input = GPIO_NUM_40, audio_output = GPIO_NUM_52, amplifier = GPIO_NUM_45;
}

esp_err_t mosaico_board_init();
esp_err_t mosaico_set_amplifier(bool enabled);
gpio_num_t mosaico_display_clock();
gpio_num_t mosaico_display_reset();
esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
