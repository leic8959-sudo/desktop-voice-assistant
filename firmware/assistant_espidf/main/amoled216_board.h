#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#define BSP_CAPS_TOUCH 1
#if defined(ASSISTANT_BOARD_AMOLED_175C) || defined(ASSISTANT_BOARD_AMOLED_132)
#define BSP_CAPS_SDCARD 0
#else
#define BSP_CAPS_SDCARD 1
#define BSP_SD_MOUNT_POINT "/sdcard"
#endif
#define BSP_POWER_AMP_IO GPIO_NUM_46

namespace amoled_board {
#if defined(ASSISTANT_BOARD_SH8601)
inline constexpr int width = 368, height = 448;
#elif defined(ASSISTANT_BOARD_AMOLED_206)
inline constexpr int width = 410, height = 502;
#elif defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C) || defined(ASSISTANT_BOARD_AMOLED_132)
inline constexpr int width = 466, height = 466;
#else
inline constexpr int width = 480, height = 480;
#endif
#if defined(ASSISTANT_BOARD_SH8601)
inline constexpr gpio_num_t panel_reset = GPIO_NUM_NC, touch_reset = GPIO_NUM_NC;
#elif defined(ASSISTANT_BOARD_AMOLED_132)
inline constexpr gpio_num_t panel_reset = GPIO_NUM_8, touch_reset = GPIO_NUM_7;
#elif defined(ASSISTANT_BOARD_AMOLED_206)
inline constexpr gpio_num_t panel_reset = GPIO_NUM_8, touch_reset = GPIO_NUM_9;
#elif defined(ASSISTANT_BOARD_AMOLED_175C)
inline constexpr gpio_num_t panel_reset = GPIO_NUM_1, touch_reset = GPIO_NUM_2;
#else
inline constexpr gpio_num_t panel_reset = GPIO_NUM_39, touch_reset = GPIO_NUM_40;
#endif
}

namespace spi_board {
#if defined(ASSISTANT_BOARD_AMOLED_132)
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_47, i2c_scl = GPIO_NUM_48;
inline constexpr gpio_num_t mclk = GPIO_NUM_38, bclk = GPIO_NUM_39, word_select = GPIO_NUM_41;
inline constexpr gpio_num_t audio_output = GPIO_NUM_42, audio_input = GPIO_NUM_40;
#else
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_15, i2c_scl = GPIO_NUM_14;
#if defined(ASSISTANT_BOARD_SH8601) || defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_AMOLED_175C)
inline constexpr gpio_num_t mclk = GPIO_NUM_16;
#else
inline constexpr gpio_num_t mclk = GPIO_NUM_42;
#endif
#if defined(ASSISTANT_BOARD_AMOLED_206)
inline constexpr gpio_num_t bclk = GPIO_NUM_41, audio_output = GPIO_NUM_40, audio_input = GPIO_NUM_42;
#else
inline constexpr gpio_num_t bclk = GPIO_NUM_9, audio_output = GPIO_NUM_8, audio_input = GPIO_NUM_10;
#endif
inline constexpr gpio_num_t word_select = GPIO_NUM_45;
#endif
inline constexpr gpio_num_t amplifier = GPIO_NUM_46;
}

esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
#if BSP_CAPS_SDCARD
esp_err_t bsp_sdcard_mount();
#endif
esp_err_t amoled216_set_amplifier(bool enabled);
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
