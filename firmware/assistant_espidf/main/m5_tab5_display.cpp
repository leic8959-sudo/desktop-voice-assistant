#include "m5_tab5_board.h"
#include "m5_tab5_panel_commands.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "third_party/xiaozhi_tab5/esp_lcd_touch_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "third_party/xiaozhi_tab5/esp_lcd_st7121.h"

namespace {
constexpr const char *TAG = "m5_tab5_display";
constexpr int kWidth = 720, kHeight = 1280, kDrawRows = 20;
enum class Panel { Ili9881c, St7121, St7123 };
Panel g_kind;
bool g_attempted, g_ready, g_pwm_ready, g_wait_for_release = true, g_touch_failed;
esp_lcd_dsi_bus_handle_t g_dsi;
esp_ldo_channel_handle_t g_phy_power;
esp_lcd_panel_io_handle_t g_panel_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;

esp_err_t touch_io(int address) {
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = address; io.scl_speed_hz = 100000;
    io.control_phase_bytes = 1; io.lcd_cmd_bits = 16;
    io.flags.disable_control_phase = true; io.transaction_timeout_ms = 100;
    return esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &io, &g_touch_io);
}

esp_err_t identify_panel() {
    const esp_err_t probe = i2c_master_probe(bsp_i2c_get_handle(), 0x55, 200);
    if (probe == ESP_ERR_NOT_FOUND) {
        // Only a definite absent ST address can select the owning GT911 path.
        ESP_RETURN_ON_ERROR(i2c_master_probe(bsp_i2c_get_handle(), 0x14, 200), TAG, "GT911 known address");
        g_kind = Panel::Ili9881c;
        return touch_io(0x14);
    }
    ESP_RETURN_ON_ERROR(probe, TAG, "ST touch probe");
    ESP_RETURN_ON_ERROR(touch_io(0x55), TAG, "ST detection IO");
    uint8_t first = 0, second = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(g_touch_io, 0x0000, &first, 1), TAG, "ST firmware first read");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(g_touch_io, 0x0000, &second, 1), TAG, "ST firmware repeat read");
    if (first != second || (first != 1 && first != 3)) {
        ESP_LOGE(TAG, "panel identity unconfirmed firmware=%u/%u;restart required", first, second);
        return ESP_ERR_INVALID_RESPONSE;
    }
    g_kind = first == 1 ? Panel::St7121 : Panel::St7123;
    return ESP_OK;
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    esp_lcd_touch_point_data_t points[2]{};
    uint8_t count = 0;
    esp_err_t result = esp_lcd_touch_read_data(g_touch);
    if (result == ESP_OK) result = esp_lcd_touch_get_data(g_touch, points, &count, 2);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "touch failed:%s", esp_err_to_name(result));
        g_touch_failed = true; g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch recovered;waiting for release");
    g_touch_failed = false;
    if (!count) { g_wait_for_release = false; return; }
    if (count != 1 || points[0].x >= kWidth || points[0].y >= kHeight) {
        g_wait_for_release = true; lv_indev_reset(pointer, nullptr); return;
    }
    if (g_wait_for_release) return;
    data->point.x = points[0].x; data->point.y = points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t darken() {
    if (!g_pwm_ready) return gpio_set_level(GPIO_NUM_22, 0);
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0), TAG, "dark duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t initialize() {
    ESP_RETURN_ON_ERROR(m5_tab5_board_init(), TAG, "power and I2C");
    ESP_RETURN_ON_ERROR(m5_tab5_reset_display(), TAG, "LCD/touch reset");
    ESP_RETURN_ON_ERROR(identify_panel(), TAG, "panel identity");
    const bool ili = g_kind == Panel::Ili9881c, st1 = g_kind == Panel::St7121;
    const char *name = ili ? "ILI9881C" : st1 ? "ST7121" : "ST7123";
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_10_BIT; timer.freq_hz = 5000; timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {};
    channel.gpio_num = GPIO_NUM_22; channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0; channel.timer_sel = LEDC_TIMER_0; channel.duty = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight PWM");
    g_pwm_ready = true;
    esp_ldo_channel_config_t power = {};
    power.chan_id = 3; power.voltage_mv = 2500;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &g_phy_power), TAG, "PHY LDO3");
    esp_lcd_dsi_bus_config_t bus = {};
    bus.bus_id = 0; bus.num_data_lanes = 2; bus.lane_bit_rate_mbps = ili ? 900 : 965;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &g_dsi), TAG, "DSI bus");
    esp_lcd_dbi_io_config_t io = {};
    io.virtual_channel = 0; io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(g_dsi, &io, &g_panel_io), TAG, "DBI IO");
    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT; dpi.dpi_clock_freq_mhz = ili ? 60 : 70;
    dpi.in_color_format = LCD_COLOR_FMT_RGB565; dpi.out_color_format = LCD_COLOR_FMT_RGB565;
    dpi.num_fbs = ili ? 2 : 1; dpi.flags.use_dma2d = true;
    dpi.video_timing.h_size = kWidth; dpi.video_timing.v_size = kHeight;
    dpi.video_timing.hsync_pulse_width = ili ? 40 : 2;
    dpi.video_timing.hsync_back_porch = ili ? 140 : 40; dpi.video_timing.hsync_front_porch = 40;
    dpi.video_timing.vsync_pulse_width = ili ? 4 : st1 ? 20 : 2;
    dpi.video_timing.vsync_back_porch = ili ? 20 : st1 ? 24 : 8;
    dpi.video_timing.vsync_front_porch = ili ? 20 : st1 ? 200 : 220;
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = GPIO_NUM_NC; config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE; config.bits_per_pixel = ili ? 16 : 24;
    // Preserve each owning driver's panel setup; DPI memory remains RGB565.
    if (ili) {
        ili9881c_vendor_config_t vendor = {};
        vendor.init_cmds = tab5_ili_commands; vendor.init_cmds_size = sizeof(tab5_ili_commands) / sizeof(tab5_ili_commands[0]);
        vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi; vendor.mipi_config.lane_num = 2;
        config.vendor_config = &vendor;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9881c(g_panel_io, &config, &g_panel), TAG, "ILI9881C create");
    } else if (st1) {
        st7121_vendor_config_t vendor = {};
        vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi;
        config.vendor_config = &vendor;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7121(g_panel_io, &config, &g_panel), TAG, "ST7121 create");
    } else {
        st7123_vendor_config_t vendor = {};
        vendor.init_cmds = tab5_st_commands; vendor.init_cmds_size = sizeof(tab5_st_commands) / sizeof(tab5_st_commands[0]);
        vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi; vendor.mipi_config.lane_num = 2;
        config.vendor_config = &vendor;
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7123(g_panel_io, &config, &g_panel), TAG, "ST7123 create");
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "panel display on");
    // ST7123 mirror/display callbacks are no-ops in the pinned driver. Keep
    // the fixed initialization direction and use the backlight for screen-off.
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kWidth; touch.y_max = kHeight;
    touch.rst_gpio_num = GPIO_NUM_NC; touch.int_gpio_num = GPIO_NUM_NC;
    if (ili) ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(g_touch_io, &touch, &g_touch), TAG, "GT911 init");
    else ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_st7123(g_touch_io, &touch, &g_touch), TAG, "ST touch init");
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "LVGL port");
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io; display.panel_handle = g_panel;
    display.hres = kWidth; display.vres = kHeight; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.buffer_size = kWidth * kDrawRows; display.double_buffer = true;
    display.flags.buff_spiram = true; display.flags.sw_rotate = true;
    lvgl_port_display_dsi_cfg_t dsi = {};
    g_display = lvgl_port_add_disp_dsi(&display, &dsi);
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display); lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    g_ready = g_display && g_pointer;
    lvgl_port_unlock();
    if (!g_ready) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "%s ready720x1280 DSI=%d DPI=%d framebuffers=%d;backlight dark", name, ili ? 900 : 965, ili ? 60 : 70, ili ? 2 : 1);
    return ESP_OK;
}
}

lv_display_t *bsp_display_start() {
    if (g_attempted) return g_ready ? g_display : nullptr;
    g_attempted = true;
    const esp_err_t result = initialize();
    if (result != ESP_OK) {
        const esp_err_t dark = darken(), mute = m5_tab5_set_amplifier(false);
        ESP_LOGE(TAG, "display stopped:%s dark=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
    }
    // Preserve partial objects that may already be referenced by callbacks.
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!percent && !g_ready) return darken();
    if (!g_ready || !g_pwm_ready) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, static_cast<uint32_t>(percent) * 1023 / 100), TAG, "brightness duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
