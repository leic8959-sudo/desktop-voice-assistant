#pragma once
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"

namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_NC, bclk = GPIO_NUM_7, word_select = GPIO_NUM_8;
inline constexpr gpio_num_t audio_input = GPIO_NUM_6, audio_output = GPIO_NUM_9, amplifier = GPIO_NUM_NC;
}
esp_err_t spot_c5_board_init();
esp_err_t spot_c5_set_amplifier(bool enabled);
esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
