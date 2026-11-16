#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#if defined(ASSISTANT_BOARD_BOXLITE)
#define BSP_CAPS_TOUCH 0
#else
#define BSP_CAPS_TOUCH 1
#endif
#if defined(ASSISTANT_BOARD_BOX3)
#define BSP_CAPS_SDCARD 1
#define BSP_SD_MOUNT_POINT "/sdcard"
#else
#define BSP_CAPS_SDCARD 0
#endif
#define BSP_BUTTON_CONFIG_IO GPIO_NUM_0
#define BSP_POWER_AMP_IO GPIO_NUM_46

namespace spi_board {
inline constexpr gpio_num_t i2c_sda = GPIO_NUM_8, i2c_scl = GPIO_NUM_18;
inline constexpr gpio_num_t mclk = GPIO_NUM_2, bclk = GPIO_NUM_17;
#if defined(ASSISTANT_BOARD_BOX3)
inline constexpr gpio_num_t word_select = GPIO_NUM_45;
#else
inline constexpr gpio_num_t word_select = GPIO_NUM_47;
#endif
inline constexpr gpio_num_t audio_output = GPIO_NUM_15, audio_input = GPIO_NUM_16, amplifier = GPIO_NUM_46;
}
namespace box_board {
#if defined(ASSISTANT_BOARD_BOX3)
inline constexpr gpio_num_t backlight = GPIO_NUM_47;
inline constexpr bool reset_active_high = true;
#else
inline constexpr gpio_num_t backlight = GPIO_NUM_45;
inline constexpr bool reset_active_high = false;
#endif
#if defined(ASSISTANT_BOARD_BOXLITE)
inline constexpr bool backlight_inverted = true;
inline constexpr bool mirror_x = false;
#else
inline constexpr bool backlight_inverted = false;
inline constexpr bool mirror_x = true;
#endif
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
esp_err_t box3_set_amplifier(bool enabled);
esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t waveshare_spi_audio_open_status();
int waveshare_spi_audio_set_microphone_gain(float gain);
int waveshare_spi_audio_set_output_mute(bool muted);
