#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"
#include "manual_input.h"

#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0
#define BSP_POWER_AMP_IO GPIO_NUM_7

namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_2, bclk = GPIO_NUM_3, word_select = GPIO_NUM_4;
inline constexpr gpio_num_t audio_input = GPIO_NUM_6, audio_output = GPIO_NUM_5, amplifier = GPIO_NUM_7;
}

esp_err_t s31_korvo_board_init();
ManualInputKey s31_korvo_read_keys();
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
int waveshare_spi_audio_write_mono(esp_codec_dev_handle_t speaker, uint8_t *data, size_t length);
