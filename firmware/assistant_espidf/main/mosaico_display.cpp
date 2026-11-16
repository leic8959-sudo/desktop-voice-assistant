#include "mosaico_board.h"
#include "mosaico_panel_commands.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "mosaico_display";
constexpr int kSize = 480, kDrawRows = 20;
esp_lcd_panel_io_handle_t g_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_attempted, g_ready, g_touch_failed, g_wait_for_release = true;

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    esp_lcd_touch_point_data_t points[2]{};
    uint8_t count = 0;
    esp_err_t result = esp_lcd_touch_read_data(g_touch);
    if (result == ESP_OK) result = esp_lcd_touch_get_data(g_touch, points, &count, 2);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "CST9220 read failed:%s", esp_err_to_name(result));
        g_touch_failed = true; g_wait_for_release = true; lv_indev_reset(pointer, nullptr); return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "CST9220 recovered;waiting for release");
    g_touch_failed = false;
    if (!count) { g_wait_for_release = false; return; }
    if (count != 1 || points[0].x >= kSize || points[0].y >= kSize) {
        g_wait_for_release = true; lv_indev_reset(pointer, nullptr); return;
    }
    if (g_wait_for_release) return;
    data->point.x = points[0].x; data->point.y = points[0].y; data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize() {
    ESP_RETURN_ON_ERROR(mosaico_board_init(), TAG, "revision/power");
    spi_bus_config_t bus = {};
    bus.sclk_io_num = mosaico_display_clock(); bus.data0_io_num = GPIO_NUM_36; bus.data1_io_num = GPIO_NUM_51;
    bus.data2_io_num = GPIO_NUM_35; bus.data3_io_num = GPIO_NUM_9;
    bus.data4_io_num = GPIO_NUM_NC; bus.data5_io_num = GPIO_NUM_NC; bus.data6_io_num = GPIO_NUM_NC; bus.data7_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = kSize * kDrawRows * 2; bus.flags = SPICOMMON_BUSFLAG_QUAD;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI bus");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_50; io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0; io.pclk_hz = 40000000; io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32; io.lcd_param_bits = 8; io.flags.quad_mode = true;
    // Use bounded internal DMA draw buffers; do not assume PSRAM-direct DMA
    // support from the upstream board's full-frame transfer configuration.
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_io), TAG, "QSPI panel IO");
    co5300_vendor_config_t vendor = {};
    vendor.init_cmds = mosaico_panel_commands;
    vendor.init_cmds_size = sizeof(mosaico_panel_commands) / sizeof(mosaico_panel_commands[0]);
    vendor.flags.use_qspi_interface = true;
    esp_lcd_panel_dev_config_t config = {};
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; config.bits_per_pixel = 16;
    config.reset_gpio_num = mosaico_display_reset(); config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(g_io, &config, &g_panel), TAG, "CO5300 create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "CO5300 reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "CO5300 commands");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(g_panel, 0), TAG, "CO5300 dark");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "CO5300 orientation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "CO5300 display on");
    esp_lcd_panel_io_i2c_config_t touch_io = {};
    touch_io.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST9220_ADDRESS; touch_io.scl_speed_hz = 400000;
    touch_io.control_phase_bytes = 1; touch_io.lcd_cmd_bits = 8; touch_io.lcd_param_bits = 8;
    touch_io.flags.disable_control_phase = true; touch_io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &touch_io, &g_touch_io), TAG, "CST9220 IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kSize - 1; touch.y_max = kSize - 1;
    touch.rst_gpio_num = GPIO_NUM_NC; touch.int_gpio_num = GPIO_NUM_6;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9220(g_touch_io, &touch, &g_touch), TAG, "CST9220 init");
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "LVGL port");
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io; display.panel_handle = g_panel;
    display.hres = kSize; display.vres = kSize; display.buffer_size = kSize * kDrawRows;
    display.double_buffer = true; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true; display.flags.swap_bytes = true;
    g_display = lvgl_port_add_disp(&display);
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
        const esp_err_t dark = g_panel ? esp_lcd_panel_co5300_set_brightness(g_panel, 0) : ESP_ERR_INVALID_STATE;
        const esp_err_t mute = mosaico_set_amplifier(false);
        ESP_LOGE(TAG, "display stopped:%s dark=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
    }
    // Keep partially created objects alive if panel/LVGL callbacks retain them.
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_panel || (!g_ready && percent)) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_co5300_set_brightness(g_panel, percent);
}
