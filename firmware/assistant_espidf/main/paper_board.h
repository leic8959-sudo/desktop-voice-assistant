#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "paper_display.h"

#define BSP_CAPS_TOUCH 0
#define BSP_CAPS_SDCARD 0
#if defined(ASSISTANT_BOARD_MAZA_S31)
#include "esp_codec_dev.h"
#define BSP_I2C_NUM I2C_NUM_1
#define BSP_POWER_AMP_IO GPIO_NUM_2
namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_7, bclk = GPIO_NUM_11, word_select = GPIO_NUM_9;
inline constexpr gpio_num_t audio_input = GPIO_NUM_10, audio_output = GPIO_NUM_8, amplifier = GPIO_NUM_2;
}
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
#else
#define BSP_I2C_NUM I2C_NUM_0
#if defined(ASSISTANT_BOARD_WS_EPAPER397)
#define BSP_I2S_MCLK GPIO_NUM_13
#define BSP_I2S_SCLK GPIO_NUM_14
#define BSP_I2S_LCLK GPIO_NUM_47
#define BSP_I2S_DSIN GPIO_NUM_21
#define BSP_I2S_DOUT GPIO_NUM_48
#define BSP_POWER_AMP_IO GPIO_NUM_39
#elif defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_ZECTRIX)
#define BSP_I2S_MCLK GPIO_NUM_14
#define BSP_I2S_SCLK GPIO_NUM_15
#define BSP_I2S_LCLK GPIO_NUM_38
#define BSP_I2S_DSIN GPIO_NUM_16
#define BSP_I2S_DOUT GPIO_NUM_45
#define BSP_POWER_AMP_IO GPIO_NUM_46
#else
#define BSP_I2S_MCLK GPIO_NUM_6
#define BSP_I2S_SCLK GPIO_NUM_5
#define BSP_I2S_LCLK GPIO_NUM_3
#define BSP_I2S_DSIN GPIO_NUM_4
#define BSP_I2S_DOUT GPIO_NUM_2
#define BSP_POWER_AMP_IO GPIO_NUM_1
#endif
#endif

esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
