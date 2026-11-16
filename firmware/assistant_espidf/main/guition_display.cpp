#include "guition_board.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "nv3041a_panel.h"

namespace {
constexpr const char *TAG = "guition_display";
constexpr int kWidth = 480, kHeight = 272;
static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS >= 2, "Guition must detect multiple touches");
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_io_handle_t g_panel_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_started, g_ready, g_pwm_ready, g_wait_for_release = true, g_touch_failed;

void cancel_press(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    const esp_err_t read_result = esp_lcd_touch_read_data(g_touch);
    esp_lcd_touch_point_data_t points[2]{};
    uint8_t count = 0;
    // Drain stale points even on failure; never turn an old press into a
    // release/click after a failed I2C read.
    const esp_err_t point_result = esp_lcd_touch_get_data(g_touch, points, &count, 2);
    if (read_result != ESP_OK || point_result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_failed read=%s data=%s", esp_err_to_name(read_result), esp_err_to_name(point_result));
        g_touch_failed = true;
        cancel_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch recovered; wait for release");
    g_touch_failed = false;
    if (!count) { g_wait_for_release = false; return; }
    if (count != 1 || points[0].x >= kWidth || points[0].y >= kHeight) {
        cancel_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = points[0].x;
    data->point.y = points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
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
    backlight.gpio_num = GPIO_NUM_1;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight initially dark");
    g_pwm_ready = true;
    spi_bus_config_t bus = {};
    bus.sclk_io_num = GPIO_NUM_47;
    bus.data0_io_num = GPIO_NUM_21;
    bus.data1_io_num = GPIO_NUM_48;
    bus.data2_io_num = GPIO_NUM_40;
    bus.data3_io_num = GPIO_NUM_39;
    bus.max_transfer_sz = kWidth * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI2");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_45;
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), TAG, "QSPI panel IO");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_NC;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_nv3041a(g_panel_io, &panel, &g_panel), TAG, "NV3041A");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset wait");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, false), TAG, "panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), TAG, "IPS inversion");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "touch I2C1");
    esp_lcd_panel_io_i2c_config_t touch_io = {};
    touch_io.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    touch_io.scl_speed_hz = 400000;
    touch_io.control_phase_bytes = 1;
    touch_io.lcd_cmd_bits = 16;
    touch_io.flags.disable_control_phase = 1;
    touch_io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &touch_io, &g_touch_io), TAG, "GT911 IO");
    // driver_data makes GT911 drive INT low during reset to select0x5D.
    // A no-ACK before this reset cannot prove the touch device is absent.
    static esp_lcd_touch_io_gt911_config_t address = {};
    address.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kWidth;
    touch.y_max = kHeight;
    touch.rst_gpio_num = GPIO_NUM_38;
    touch.int_gpio_num = GPIO_NUM_3;
    touch.levels.reset = 0;
    touch.levels.interrupt = 0;
    touch.driver_data = &address;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(g_touch_io, &touch, &g_touch), TAG, "GT911 reset/address0x5D");
    return ESP_OK;
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1;
    bus.sda_io_num = GPIO_NUM_8;
    bus.scl_io_num = GPIO_NUM_4;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

lv_display_t *bsp_display_start() {
    if (g_started) return g_ready ? g_display : nullptr;
    g_started = true;
    if (initialize_display() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io;
    display.panel_handle = g_panel;
    display.hres = kWidth;
    display.vres = kHeight;
    display.buffer_size = kWidth * 20;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true;
    display.flags.swap_bytes = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
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
    if (!g_ready) ESP_LOGE(TAG, "display/touch allocation failed");
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), TAG, "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
