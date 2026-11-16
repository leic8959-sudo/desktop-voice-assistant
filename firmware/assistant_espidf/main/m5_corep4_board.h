#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0
#define BSP_POWER_AMP_IO GPIO_NUM_NC

namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_2, bclk = GPIO_NUM_6, word_select = GPIO_NUM_4;
inline constexpr gpio_num_t audio_input = GPIO_NUM_5, audio_output = GPIO_NUM_3, amplifier = GPIO_NUM_NC;
}

esp_err_t m5_corep4_board_init();
esp_err_t m5_corep4_set_amplifier(bool enabled);
esp_err_t m5_corep4_touch_read(uint16_t *x, uint16_t *y, bool *pressed);
esp_err_t m5_corep4_brightness(int percent);
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
