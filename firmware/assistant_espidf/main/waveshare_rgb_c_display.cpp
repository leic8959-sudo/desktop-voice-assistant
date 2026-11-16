#include "board_profile.h"

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "gt911_polling.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "waveshare_rgb_c_display";
constexpr int kWidth = 800, kHeight = 480;
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_handle_t g_panel;
esp_lcd_panel_io_handle_t g_touch_io;
Gt911Polling g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_started, g_ready, g_wait_for_release = true, g_touch_failed;

void cancel_press(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    Gt911Frame frame{};
    const esp_err_t result = g_touch.Read(&frame);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_failed error=%s", esp_err_to_name(result));
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

esp_err_t initialize_rgb() {
    esp_lcd_rgb_panel_config_t panel = {};
    panel.clk_src = LCD_CLK_SRC_DEFAULT;
    panel.timings.pclk_hz = 16000000;
    panel.timings.h_res = kWidth;
    panel.timings.v_res = kHeight;
    panel.timings.hsync_pulse_width = 4;
    panel.timings.hsync_front_porch = 8;
    panel.timings.vsync_pulse_width = 4;
    panel.timings.vsync_front_porch = 8;
#if defined(ASSISTANT_BOARD_WS_RGB_7C)
    panel.timings.hsync_back_porch = 8;
    panel.timings.vsync_back_porch = 8;
#else
    panel.timings.hsync_back_porch = 4;
    panel.timings.vsync_back_porch = 4;
#endif
    panel.timings.flags.pclk_active_neg = true;
    panel.data_width = 16;
    panel.bits_per_pixel = 16;
    panel.num_fbs = 2;
    panel.bounce_buffer_size_px = kWidth * 10;
    panel.dma_burst_size = 64;
    panel.hsync_gpio_num = GPIO_NUM_46;
    panel.vsync_gpio_num = GPIO_NUM_3;
    panel.de_gpio_num = GPIO_NUM_5;
    panel.pclk_gpio_num = GPIO_NUM_7;
    panel.disp_gpio_num = GPIO_NUM_NC;
#if defined(ASSISTANT_BOARD_WS_RGB_7C)
    constexpr int data_pins[] = {14, 38, 18, 17, 10, 39, 0, 45, 9, 8, 21, 1, 2, 42, 41, 40};
#else
    constexpr int data_pins[] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
#endif
    for (int index = 0; index < 16; ++index) panel.data_gpio_nums[index] = data_pins[index];
    panel.flags.fb_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel, &g_panel), TAG, "RGB panel");
    return esp_lcd_panel_init(g_panel);
}

esp_err_t initialize_touch() {
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = 0x5d;
    io.scl_speed_hz = 400000;
    io.control_phase_bytes = 1;
    io.lcd_cmd_bits = 16;
    io.flags.disable_control_phase = 1;
    io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), TAG, "GT911 IO");
    // CH32 reset/INT address selection has already completed; do not let
    // the touch driver issue a second reset with an unowned GPIO.
    ESP_RETURN_ON_ERROR(g_touch.Init(g_touch_io), TAG, "GT911 at selected0x5D");
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
    // board_power_init has set up CH32 before this display entry.
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
esp_err_t bsp_display_brightness_set(int percent) { return waveshare_rgb_c_set_brightness(percent); }
