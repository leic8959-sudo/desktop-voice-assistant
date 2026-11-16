#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#define BSP_CAPS_TOUCH 0
#define BSP_CAPS_SDCARD 0

namespace spi_board {
inline constexpr int width = 360, height = 360;
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_26, i2c_scl = GPIO_NUM_27;
inline constexpr gpio_num_t mclk = GPIO_NUM_25, bclk = GPIO_NUM_11, word_select = GPIO_NUM_24;
inline constexpr gpio_num_t audio_output = GPIO_NUM_23, audio_input = GPIO_NUM_12;
// Board-owned INPUT_OUTPUT PA5 must retain its input readback path.
inline constexpr gpio_num_t amplifier = GPIO_NUM_NC;
}

esp_err_t moji2_c5_board_init();
esp_err_t moji2_c5_set_amplifier(bool enabled);
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
