#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0

// Fixed XiaoZhi v2.5.2 Waveshare C5 1.69 wiring. PA and touch reset are
// expander pins and must never be passed to the codec as GPIO0/GPIO1.
namespace spi_board {
inline constexpr int width = 240, height = 280;
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_9;
inline constexpr gpio_num_t mclk = GPIO_NUM_0, bclk = GPIO_NUM_1, word_select = GPIO_NUM_3;
inline constexpr gpio_num_t audio_output = GPIO_NUM_10, audio_input = GPIO_NUM_2;
inline constexpr gpio_num_t amplifier = GPIO_NUM_NC;
}

esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
esp_err_t c5_lcd169_board_init();
esp_err_t c5_lcd169_set_amplifier(bool enabled);
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
