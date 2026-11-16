#include "board_profile.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io_additions.h"
#include "waveshare_4b_commands.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "gt911_polling.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "waveshare_4b_display";
constexpr int kWidth = 480, kHeight = 480;
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_handle_t g_panel;
esp_lcd_panel_io_handle_t g_touch_io;
Gt911Polling g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_pwm_ready;
bool g_started, g_ready, g_wait_for_release = true, g_touch_failed;

void cancel_press(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    Gt911Frame frame{};
    const esp_err_t read_result = g_touch.Read(&frame);
    if (read_result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_failed read=%s", esp_err_to_name(read_result));
        g_touch_failed = true;
        cancel_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch recovered; wait for release");
    g_touch_failed = false;
    if (!frame.count) { g_wait_for_release = false; return; }
    if (frame.count != 1 || frame.points[0].x >= kWidth || frame.points[0].y >= kHeight) {
        cancel_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = frame.points[0].x;
    data->point.y = frame.points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize_controller() {
    const auto expander = waveshare_4b_get_expander();
    if (!expander) return ESP_ERR_INVALID_STATE;
    esp_lcd_panel_io_3wire_spi_config_t configuration = {};
    configuration.line_config.cs_io_type = IO_TYPE_EXPANDER;
    configuration.line_config.cs_expander_pin = IO_EXPANDER_PIN_NUM_0;
    configuration.line_config.scl_io_type = IO_TYPE_EXPANDER;
    configuration.line_config.scl_expander_pin = IO_EXPANDER_PIN_NUM_2;
    configuration.line_config.sda_io_type = IO_TYPE_EXPANDER;
    configuration.line_config.sda_expander_pin = IO_EXPANDER_PIN_NUM_1;
    configuration.line_config.io_expander = expander;
    configuration.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
    configuration.lcd_cmd_bytes = 1;
    configuration.lcd_param_bytes = 1;
    configuration.flags.use_dc_bit = true;
    configuration.flags.del_keep_cs_inactive = true;
    esp_lcd_panel_io_handle_t initialization_io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&configuration, &initialization_io), TAG, "ST7701 3-wire IO");
    // Match reviewed ST7701 2.0.2~2 auto_del_panel_io lifecycle: SWRESET,
    // bank00/MADCTL00/COLMOD60, then the owning board's35 commands. Keep
    // its18-bit controller parameter and final COLMOD66 despite RGB565 bus;
    // do not silently rewrite the manufacturer's table to55.
    esp_err_t result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_SWRESET, nullptr, 0);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(120));
    const uint8_t bank[] = {0x77, 0x01, 0x00, 0x00, 0x00};
    const uint8_t orientation = 0x00, format = 0x60;
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, 0xff, bank, sizeof(bank));
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_MADCTL, &orientation, 1);
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_COLMOD, &format, 1);
    for (const auto &command : WAVESHARE_4B_COMMANDS) {
        if (result != ESP_OK) break;
        result = esp_lcd_panel_io_tx_param(initialization_io, command.code,
            command.length ? command.data : nullptr, command.length);
        if (result != ESP_OK) ESP_LOGE(TAG, "controller_command_failed command=0x%02x error=%s", command.code, esp_err_to_name(result));
        if (result == ESP_OK && command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    // Always delete initialization-only IO. Its CS remains inactive and no
    // dangling handle reaches LVGL. No runtime3wire writes race the PA.
    const esp_err_t cleanup = esp_lcd_panel_io_del(initialization_io);
    if (cleanup != ESP_OK) ESP_LOGE(TAG, "initialization_io_cleanup_failed error=%s", esp_err_to_name(cleanup));
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "controller_initialization_failed error=%s", esp_err_to_name(result));
        return result;
    }
    return cleanup;
}

esp_err_t initialize_rgb() {
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
    backlight.duty = 1023;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "active-low backlight initially dark");
    g_pwm_ready = true;
    ESP_RETURN_ON_ERROR(initialize_controller(), TAG, "ST7701 initialization");
    esp_lcd_rgb_panel_config_t panel = {};
    panel.clk_src = LCD_CLK_SRC_DEFAULT;
    panel.timings.pclk_hz = 16000000;
    panel.timings.h_res = kWidth;
    panel.timings.v_res = kHeight;
    panel.timings.hsync_pulse_width = 10;
    panel.timings.hsync_back_porch = 10;
    panel.timings.hsync_front_porch = 20;
    panel.timings.vsync_pulse_width = 10;
    panel.timings.vsync_back_porch = 10;
    panel.timings.vsync_front_porch = 10;
    panel.timings.flags.pclk_active_neg = false;
    panel.data_width = 16;
    panel.bits_per_pixel = 16;
    panel.num_fbs = 2;
    panel.bounce_buffer_size_px = kWidth * 20;
    panel.dma_burst_size = 64;
    panel.hsync_gpio_num = GPIO_NUM_46;
    panel.vsync_gpio_num = GPIO_NUM_3;
    panel.de_gpio_num = GPIO_NUM_17;
    panel.pclk_gpio_num = GPIO_NUM_9;
    panel.disp_gpio_num = GPIO_NUM_NC;
    constexpr int data_pins[] = {40, 41, 42, 2, 1, 21, 8, 18, 45, 38, 39, 10, 11, 12, 13, 14};
    for (int index = 0; index < 16; ++index) panel.data_gpio_nums[index] = data_pins[index];
    panel.flags.fb_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel, &g_panel), TAG, "RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "RGB start");
    ESP_LOGI(TAG, "RGB native480x480 PCLK16MHz expected_refresh_millihz=60332 frames=2 bounce_rows=20 controllerCOLMOD66");
    return ESP_OK;
}

esp_err_t initialize_touch() {
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = 0x5d; // Address selected by the existing board-owned reset/INT sequence.
    io.scl_speed_hz = 400000;
    io.control_phase_bytes = 1;
    io.lcd_cmd_bits = 16;
    io.flags.disable_control_phase = 1;
    io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), TAG, "GT911 IO");
    // TCA9554 P5/P6 owns reset/address selection; polling does not change GPIOs.
    return g_touch.Init(g_touch_io);
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
    // board_power_init has set up TCA9554 and AXP2101 ALDO1 before this entry.
    if (bsp_i2c_init() != ESP_OK || initialize_rgb() != ESP_OK || initialize_touch() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.panel_handle = g_panel;
    display.hres = kWidth;
    display.vres = kHeight;
    display.buffer_size = kWidth * kHeight;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.direct_mode = true;
    // Parallel RGB uses native little-endian framebuffers; never swap their
    // bytes in place or register a nonexistent SPI IO callback.
    lvgl_port_display_rgb_cfg_t rgb = {};
    rgb.flags.bb_mode = true;
    rgb.flags.avoid_tearing = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp_rgb(&display, &rgb);
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display);
            lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    g_ready = g_display && g_pointer;
    lvgl_port_unlock();
    if (!g_ready) ESP_LOGE(TAG, "RGB/touch registration failed");
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (100 - percent) * 1023 / 100), TAG, "active-low backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
