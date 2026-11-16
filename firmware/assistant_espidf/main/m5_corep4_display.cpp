#include "m5_corep4_board.h"
#include "m5_corep4_panel_commands.h"

#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 2)
#error "CoreP4X candidate requires the reviewed IDF6.0.2+ interface"
#endif

namespace {
constexpr const char *TAG = "m5_corep4_display";
constexpr int kWidth = 480, kHeight = 480;
esp_lcd_dsi_bus_handle_t g_dsi;
esp_ldo_channel_handle_t g_phy_power;
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_attempted, g_ready, g_touch_failed, g_wait_for_release = true;

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    const esp_err_t result = m5_corep4_touch_read(&x, &y, &pressed);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "CST3530 read failed:%s", esp_err_to_name(result));
        g_touch_failed = true; g_wait_for_release = true; lv_indev_reset(pointer, nullptr); return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "CST3530 recovered;waiting for release");
    g_touch_failed = false;
    if (!pressed) { g_wait_for_release = false; return; }
    if (g_wait_for_release) return;
    // LVGL applies the display's180degree rotation to input coordinates.
    data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize() {
    ESP_RETURN_ON_ERROR(m5_corep4_board_init(), TAG, "power/touch");
    esp_ldo_channel_config_t power = {};
    power.chan_id = 3; power.voltage_mv = 2500;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &g_phy_power), TAG, "PHY LDO3");
    esp_lcd_dsi_bus_config_t bus = {};
    bus.bus_id = 0; bus.num_data_lanes = 2; bus.lane_bit_rate_mbps = 600;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &g_dsi), TAG, "DSI bus");
    esp_lcd_dbi_io_config_t io = {};
    io.virtual_channel = 0; io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(g_dsi, &io, &g_io), TAG, "DBI IO");
    vTaskDelay(pdMS_TO_TICKS(50));
    // M5GFX ST7102 resets before starting DPI, then sends its command lists.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x01, nullptr, 0), TAG, "ST7102 software reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT; dpi.dpi_clock_freq_mhz = 24;
    dpi.in_color_format = LCD_COLOR_FMT_RGB565; dpi.out_color_format = LCD_COLOR_FMT_RGB565;
    dpi.num_fbs = 2;
    dpi.video_timing.h_size = kWidth; dpi.video_timing.v_size = kHeight;
    dpi.video_timing.hsync_pulse_width = 2; dpi.video_timing.hsync_back_porch = 40; dpi.video_timing.hsync_front_porch = 40;
    dpi.video_timing.vsync_pulse_width = 4; dpi.video_timing.vsync_back_porch = 8; dpi.video_timing.vsync_front_porch = 200;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(g_dsi, &dpi, &g_panel), TAG, "DPI panel");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_enable_dma2d(g_panel), TAG, "IDF6 DPI DMA2D");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "DPI start");
    for (const auto &command : corep4_panel_commands) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, command.command, command.data, command.length), TAG, "ST7102 initialization command");
        if (command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "LVGL port");
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io; display.panel_handle = g_panel;
    display.hres = kWidth; display.vres = kHeight; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.buffer_size = kWidth * 20; display.double_buffer = true;
    display.flags.buff_spiram = true; display.flags.sw_rotate = true;
    lvgl_port_display_dsi_cfg_t dsi = {};
    g_display = lvgl_port_add_disp_dsi(&display, &dsi);
    if (g_display) {
        // Manufacturer CoreP4X metadata sets rotation_offset2 for LCD/touch.
        lv_display_set_rotation(g_display, LV_DISPLAY_ROTATION_180);
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display); lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    g_ready = g_display && g_pointer;
    lvgl_port_unlock();
    if (!g_ready) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "ST7102/CST3530 ready480x480 DSI600Mbps DPI24MHz;2RGB565frames/PWMdark");
    return ESP_OK;
}
}

lv_display_t *bsp_display_start() {
    if (g_attempted) return g_ready ? g_display : nullptr;
    g_attempted = true;
    const esp_err_t result = initialize();
    if (result != ESP_OK) {
        const esp_err_t dark = m5_corep4_brightness(0), mute = m5_corep4_set_amplifier(false);
        ESP_LOGE(TAG, "display stopped:%s BL=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
    }
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready && percent) return ESP_ERR_INVALID_STATE;
    return m5_corep4_brightness(percent);
}
