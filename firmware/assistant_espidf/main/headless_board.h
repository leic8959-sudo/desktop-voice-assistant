#pragma once
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#if defined(ASSISTANT_BOARD_SPOT_C5)
#include "spot_c5_board.h"
#elif defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || defined(ASSISTANT_BOARD_S31_CORE)
#include "driver/i2c_master.h"
#if defined(ASSISTANT_BOARD_S31_CORE)
#define BSP_POWER_AMP_IO GPIO_NUM_57
namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_52, bclk = GPIO_NUM_53, word_select = GPIO_NUM_55;
inline constexpr gpio_num_t audio_input = GPIO_NUM_54, audio_output = GPIO_NUM_56, amplifier = GPIO_NUM_57;
}
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
#elif defined(ASSISTANT_BOARD_ATOM_M12_BASE)
#include "atomic_echo_base.h"
#define BSP_I2S_MCLK GPIO_NUM_NC
#define BSP_I2S_SCLK GPIO_NUM_8
#define BSP_I2S_LCLK GPIO_NUM_6
#define BSP_I2S_DSIN GPIO_NUM_7
#define BSP_I2S_DOUT GPIO_NUM_5
#define BSP_POWER_AMP_IO GPIO_NUM_NC
#else
#define BSP_I2S_MCLK GPIO_NUM_11
#define BSP_I2S_SCLK GPIO_NUM_17
#define BSP_I2S_LCLK GPIO_NUM_3
#define BSP_I2S_DSIN GPIO_NUM_4
#define BSP_I2S_DOUT GPIO_NUM_48
#define BSP_POWER_AMP_IO GPIO_NUM_18
#endif
#define BSP_I2C_NUM I2C_NUM_0
esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
#else
#include "digital_audio.h"
#endif

#define BSP_CAPS_TOUCH 0
#define BSP_CAPS_SDCARD 0
