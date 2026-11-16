#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"
#include "manual_input.h"

// Knob push is the supported local input. Touch/rotation and HX6538 are
// separate peripherals pending their own implementation and acceptance.
#define BSP_CAPS_TOUCH 0
#define BSP_CAPS_SDCARD 0
#define BSP_I2C_NUM I2C_NUM_0
#define BSP_POWER_AMP_IO GPIO_NUM_NC
namespace spi_board {
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_10, bclk = GPIO_NUM_11, word_select = GPIO_NUM_12;
inline constexpr gpio_num_t audio_output = GPIO_NUM_16, audio_input = GPIO_NUM_15, amplifier = GPIO_NUM_NC;
}
esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
esp_err_t sensecap_board_init();
esp_err_t sensecap_set_amplifier(bool enabled);
ManualInputKey sensecap_read_key();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
