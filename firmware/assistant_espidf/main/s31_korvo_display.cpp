#include "s31_korvo_board.h"

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt1151.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "s31_korvo_display";
constexpr int kWidth = 800, kHeight = 480;
esp_lcd_panel_handle_t g_panel;
esp_lcd_panel_io_handle_t g_touch_io;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_attempted, g_ready, g_touch_failed, g_wait_for_release = true;

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    uint8_t status = 0, packet[11]{};
    esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x814e, &status, 1);
    uint8_t count = status & 0x0f;
    // Same public GT1151 packet/checksum/checked-clear path as LCD EV SUB2.
    if (result == ESP_OK && count == 1) {
        result = esp_lcd_panel_io_rx_param(g_touch_io, 0x814e, packet, sizeof(packet));
        uint8_t checksum = 0;
        for (uint8_t value : packet) checksum += value;
        if (result == ESP_OK && checksum) result = ESP_ERR_INVALID_CRC;
        if (result == ESP_OK && (packet[0] & 0x0f) != 1) result = ESP_ERR_INVALID_RESPONSE;
    }
    const uint8_t clear = 0;
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(g_touch_io, 0x814e, &clear, 1);
    const uint16_t x = static_cast<uint16_t>(packet[3]) << 8 | packet[2];
    const uint16_t y = static_cast<uint16_t>(packet[5]) << 8 | packet[4];
    if (result != ESP_OK || count > 1 || (count && (x >= kWidth || y >= kHeight))) {
        if (result != ESP_OK && !g_touch_failed) ESP_LOGE(TAG, "GT1151 read failed:%s", esp_err_to_name(result));
        g_touch_failed = result != ESP_OK; g_wait_for_release = true; lv_indev_reset(pointer, nullptr); return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "GT1151 recovered;waiting for release");
    g_touch_failed = false;
    if (!count) { g_wait_for_release = false; return; }
    if (g_wait_for_release) return;
    data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize() {
    ESP_RETURN_ON_ERROR(s31_korvo_board_init(), TAG, "power/keys/I2C");
    esp_lcd_rgb_panel_config_t rgb = {};
    rgb.clk_src = LCD_CLK_SRC_DEFAULT;
    rgb.timings.pclk_hz = 16000000; rgb.timings.h_res = kWidth; rgb.timings.v_res = kHeight;
    rgb.timings.hsync_pulse_width = 4; rgb.timings.hsync_back_porch = 8; rgb.timings.hsync_front_porch = 8;
    rgb.timings.vsync_pulse_width = 4; rgb.timings.vsync_back_porch = 8; rgb.timings.vsync_front_porch = 8;
    rgb.timings.flags.pclk_active_neg = true;
    rgb.data_width = 16; rgb.in_color_format = LCD_COLOR_FMT_RGB565; rgb.out_color_format = LCD_COLOR_FMT_RGB565;
    rgb.num_fbs = 2; rgb.bounce_buffer_size_px = kWidth * 8;
    rgb.hsync_gpio_num = GPIO_NUM_44; rgb.vsync_gpio_num = GPIO_NUM_45; rgb.de_gpio_num = GPIO_NUM_43;
    rgb.pclk_gpio_num = GPIO_NUM_40; rgb.disp_gpio_num = GPIO_NUM_38;
    constexpr gpio_num_t pins[] = {GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15,
        GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_33, GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36};
    for (unsigned i = 0; i < 16; ++i) rgb.data_gpio_nums[i] = pins[i];
    rgb.flags.fb_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&rgb, &g_panel), TAG, "RGB panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "RGB start");
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT1151_ADDRESS; io.scl_speed_hz = 400000;
    io.control_phase_bytes = 1; io.lcd_cmd_bits = 16; io.flags.disable_control_phase = true; io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &io, &g_touch_io), TAG, "GT1151 IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kWidth - 1; touch.y_max = kHeight - 1;
    touch.rst_gpio_num = GPIO_NUM_NC; touch.int_gpio_num = GPIO_NUM_NC;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt1151(g_touch_io, &touch, &g_touch), TAG, "GT1151 init");
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "LVGL port");
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lvgl_port_display_cfg_t display = {};
    display.panel_handle = g_panel; display.hres = kWidth; display.vres = kHeight;
    display.buffer_size = kWidth * kHeight; display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565; display.flags.direct_mode = true;
    lvgl_port_display_rgb_cfg_t configuration = {};
    configuration.flags.bb_mode = true; configuration.flags.avoid_tearing = true;
    g_display = lvgl_port_add_disp_rgb(&display, &configuration);
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display); lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    g_ready = g_display && g_pointer;
    lvgl_port_unlock();
    return g_ready ? ESP_OK : ESP_ERR_NO_MEM;
}
}

lv_display_t *bsp_display_start() {
    if (g_attempted) return g_ready ? g_display : nullptr;
    g_attempted = true;
    const esp_err_t result = initialize();
    if (result != ESP_OK) {
        const esp_err_t mute = gpio_set_level(GPIO_NUM_7, 0);
        const esp_err_t blank = g_panel ? esp_lcd_panel_disp_on_off(g_panel, false) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "display stopped:%s PA=%s scan_enable=%s;backlight always powered/restart required", esp_err_to_name(result), esp_err_to_name(mute), esp_err_to_name(blank));
    }
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_panel || (!g_ready && percent)) return ESP_ERR_INVALID_STATE;
    // The backlight is wired on. DISP38 controls the panel image, not PWM
    // luminance; nonzero settings therefore all select the same on state.
    const esp_err_t result = esp_lcd_panel_disp_on_off(g_panel, percent != 0);
    if (result == ESP_OK) ESP_LOGI(TAG, "display requested=%d applied_scan_enable=%d backlight=always_powered", percent, percent != 0);
    return result;
}
