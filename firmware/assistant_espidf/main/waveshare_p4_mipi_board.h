#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"

#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0
#define BSP_POWER_AMP_IO GPIO_NUM_53

#if (defined(ASSISTANT_BOARD_P4_7B) + defined(ASSISTANT_BOARD_P4_ILI9881C) + defined(ASSISTANT_BOARD_P4_ST7701) + defined(ASSISTANT_BOARD_P4_HX8394) + defined(ASSISTANT_BOARD_P4_ST7703) + defined(ASSISTANT_BOARD_P4_JD9365_34C) + defined(ASSISTANT_BOARD_P4_JD9365_4C) + defined(ASSISTANT_BOARD_P4_JD9365_8) + defined(ASSISTANT_BOARD_P4_JD9365_101) + defined(ASSISTANT_BOARD_P4_NANO)) != 1
#error "Select exactly one implemented Waveshare P4 MIPI panel"
#endif

namespace waveshare_p4_mipi {
struct PanelConfig {
    const char *name;
    int width, height;
    gpio_num_t reset, backlight;
    bool reset_active_high, backlight_active_low;
    int lane_rate_mbps, dpi_clock_mhz;
    int hsync, hback, hfront, vsync, vback, vfront;
};

// Exact values from the fixed owning board class, kept independent of the
// driver helper macros, whose defaults may describe another panel size.
#if defined(ASSISTANT_BOARD_P4_7B)
inline constexpr PanelConfig panel = {"EK79007", 1024, 600, GPIO_NUM_33, GPIO_NUM_32, false, true, 900, 52, 10, 160, 160, 1, 23, 12};
#elif defined(ASSISTANT_BOARD_P4_ILI9881C)
inline constexpr PanelConfig panel = {"ILI9881C", 720, 1280, GPIO_NUM_27, GPIO_NUM_26, false, false, 1000, 80, 50, 239, 33, 30, 20, 2};
#elif defined(ASSISTANT_BOARD_P4_ST7701)
inline constexpr PanelConfig panel = {"ST7701", 480, 800, GPIO_NUM_27, GPIO_NUM_26, false, true, 500, 30, 12, 42, 42, 8, 2, 60};
#elif defined(ASSISTANT_BOARD_P4_ST7703)
inline constexpr PanelConfig panel = {"ST7703", 720, 720, GPIO_NUM_27, GPIO_NUM_26, false, true, 480, 46, 20, 80, 80, 4, 12, 30};
#elif defined(ASSISTANT_BOARD_P4_JD9365_34C)
inline constexpr PanelConfig panel = {"JD9365-3.4C", 800, 800, GPIO_NUM_27, GPIO_NUM_26, false, true, 1500, 46, 20, 20, 40, 4, 12, 24};
#elif defined(ASSISTANT_BOARD_P4_JD9365_4C)
inline constexpr PanelConfig panel = {"JD9365-4C", 720, 720, GPIO_NUM_27, GPIO_NUM_26, false, true, 1500, 46, 20, 20, 40, 4, 12, 24};
#elif defined(ASSISTANT_BOARD_P4_JD9365_8)
inline constexpr PanelConfig panel = {"JD9365-8", 800, 1280, GPIO_NUM_27, GPIO_NUM_26, false, false, 1500, 52, 20, 20, 40, 4, 10, 30};
#elif defined(ASSISTANT_BOARD_P4_JD9365_101)
inline constexpr PanelConfig panel = {"JD9365-10.1", 800, 1280, GPIO_NUM_27, GPIO_NUM_26, false, false, 1500, 52, 20, 20, 40, 4, 10, 30};
#elif defined(ASSISTANT_BOARD_P4_NANO)
inline constexpr PanelConfig panel = {"JD9365-Nano-10.1A", 800, 1280, GPIO_NUM_NC, GPIO_NUM_NC, false, false, 1500, 80, 20, 20, 40, 10, 4, 30};
#else
inline constexpr PanelConfig panel = {"HX8394", 720, 1280, GPIO_NUM_27, GPIO_NUM_26, true, false, 700, 58, 20, 20, 40, 4, 10, 24};
#endif
inline constexpr int draw_rows = 20;
inline constexpr int backlight_off_level = panel.backlight_active_low ? 1 : 0;
inline constexpr uint32_t backlight_duty(int percent) {
    return static_cast<uint32_t>(panel.backlight_active_low ? 100 - percent : percent) * 1023 / 100;
}
}

namespace spi_board {
inline constexpr gpio_num_t mclk = GPIO_NUM_13, bclk = GPIO_NUM_12, word_select = GPIO_NUM_10;
inline constexpr gpio_num_t audio_input = GPIO_NUM_11, audio_output = GPIO_NUM_9, amplifier = GPIO_NUM_53;
}

// Runs before UI memory allocation; it does not allocate the panel or LVGL.
esp_err_t waveshare_p4_mipi_prepare();
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
