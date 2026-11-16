#include "board_profile.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kevin_rgb_commands.h"

namespace {
constexpr const char *TAG = "kevin_rgb_display";
constexpr int kWidth = 376, kHeight = 960;
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
bool g_started, g_ready, g_pwm_ready;

esp_err_t initialize_controller() {
    esp_lcd_panel_io_3wire_spi_config_t configuration = {};
    configuration.line_config.cs_io_type = IO_TYPE_GPIO;
    configuration.line_config.cs_gpio_num = GPIO_NUM_48;
    configuration.line_config.scl_io_type = IO_TYPE_GPIO;
    configuration.line_config.scl_gpio_num = GPIO_NUM_17;
    configuration.line_config.sda_io_type = IO_TYPE_GPIO;
    configuration.line_config.sda_gpio_num = GPIO_NUM_16;
    configuration.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
    configuration.lcd_cmd_bytes = 1;
    configuration.lcd_param_bytes = 1;
    configuration.flags.use_dc_bit = true;
    configuration.flags.del_keep_cs_inactive = true;
    esp_lcd_panel_io_handle_t initialization_io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&configuration, &initialization_io), TAG, "controller 3-wire IO");
    esp_err_t result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_SWRESET, nullptr, 0);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(120));
    // Match the fixed driver's RGB/default MADCTL and16-bit COLMOD preamble.
    const uint8_t orientation = 0x10, format = 0x50;
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, 0xb1, &orientation, 1);
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_COLMOD, &format, 1);
    for (const auto &command : KEVIN_RGB_COMMANDS) {
        if (result != ESP_OK) break;
        result = esp_lcd_panel_io_tx_param(initialization_io, command.code,
            command.length ? command.data : nullptr, command.length);
        if (result != ESP_OK) ESP_LOGE(TAG, "controller_command_failed command=0x%02x error=%s", command.code, esp_err_to_name(result));
        if (result == ESP_OK && command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    // SCL17/SDA16 become RGB data14/data15. Always remove this IO, even if
    // controller initialization failed. Never pass it to LVGL after deletion.
    const esp_err_t cleanup = esp_lcd_panel_io_del(initialization_io);
    if (cleanup != ESP_OK) ESP_LOGE(TAG, "initialization_io_cleanup_failed error=%s", esp_err_to_name(cleanup));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "controller_initialization_failed error=%s", esp_err_to_name(result));
        return result;
    }
    return cleanup;
}

esp_err_t initialize_display() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = GPIO_NUM_4;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight initially dark");
    g_pwm_ready = true;
    ESP_RETURN_ON_ERROR(initialize_controller(), TAG, "GC9503V initialization");
    esp_lcd_rgb_panel_config_t rgb = {};
    rgb.clk_src = LCD_CLK_SRC_PLL160M;
    rgb.timings.pclk_hz = 16000000;
    rgb.timings.h_res = kWidth;
    rgb.timings.v_res = kHeight;
    rgb.timings.hsync_pulse_width = 8;
    rgb.timings.hsync_back_porch = 30;
    rgb.timings.hsync_front_porch = 30;
    rgb.timings.vsync_pulse_width = 8;
    rgb.timings.vsync_back_porch = 16;
    rgb.timings.vsync_front_porch = 16;
    rgb.data_width = 16;
    rgb.bits_per_pixel = 16;
    rgb.num_fbs = 2;
    rgb.bounce_buffer_size_px = kWidth * 10;
    rgb.dma_burst_size = 64;
    rgb.hsync_gpio_num = GPIO_NUM_6;
    rgb.vsync_gpio_num = GPIO_NUM_5;
    rgb.de_gpio_num = GPIO_NUM_15;
    rgb.pclk_gpio_num = GPIO_NUM_7;
    rgb.disp_gpio_num = GPIO_NUM_NC;
    constexpr int data_pins[] = {47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 20, 19, 8, 18, 17, 16};
    for (int index = 0; index < 16; ++index) rgb.data_gpio_nums[index] = data_pins[index];
    rgb.flags.fb_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&rgb, &g_panel), TAG, "RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "RGB start");
    ESP_LOGI(TAG, "RGB native_scan=376x960 PCLK=16000000 expected_refresh_millihz=36036 framebuffers=2 bounce_rows=10");
    return ESP_OK;
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = spi_board::i2c_sda;
    bus.scl_io_num = spi_board::i2c_scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

lv_display_t *bsp_display_start() {
    if (g_started) return g_ready ? g_display : nullptr;
    g_started = true;
    if (bsp_i2c_init() != ESP_OK || initialize_display() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.panel_handle = g_panel;
    // No io_handle: this is RGB and the initialization-only IO was deleted.
    display.hres = kWidth;
    display.vres = kHeight;
    display.buffer_size = kWidth * kHeight;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.direct_mode = true;
    // Native little-endian RGB565 matches the parallel pins. With internal
    // framebuffers the RGB driver does not rotate or mirror pixel contents;
    // keep native376x960 rather than copying the source's inconsistent swap.
    lvgl_port_display_rgb_cfg_t rgb = {};
    rgb.flags.bb_mode = true;
    rgb.flags.avoid_tearing = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp_rgb(&display, &rgb);
    g_ready = g_display != nullptr;
    lvgl_port_unlock();
    if (!g_ready) ESP_LOGE(TAG, "RGB LVGL registration failed");
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), TAG, "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
