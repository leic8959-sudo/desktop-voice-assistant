#include "board_profile.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "cardputer_display";
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
bool g_started, g_ready, g_pwm_ready;

esp_err_t initialize_display() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_8_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 256;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = GPIO_NUM_38;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight initially dark");
    g_pwm_ready = true;
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_35;
    bus.miso_io_num = GPIO_NUM_NC;
    bus.sclk_io_num = GPIO_NUM_36;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 240 * 10 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "display SPI3");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_37;
    io.dc_gpio_num = GPIO_NUM_34;
    io.spi_mode = 0;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.flags.sio_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &g_io), TAG, "ST7789 IO");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_33;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_io, &panel, &g_panel), TAG, "ST7789V2 compatible panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), TAG, "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "landscape axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 40, 53), TAG, "visible window");
    return esp_lcd_panel_disp_on_off(g_panel, true);
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
    if (initialize_display() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK || !lvgl_port_lock(1000)) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io;
    display.panel_handle = g_panel;
    display.hres = 240;
    display.vres = 135;
    display.buffer_size = 240 * 10;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true;
    display.flags.swap_bytes = true;
    display.rotation.swap_xy = true;
    display.rotation.mirror_x = true;
    g_display = lvgl_port_add_disp(&display);
    g_ready = g_display != nullptr;
    lvgl_port_unlock();
    if (!g_ready) ESP_LOGE(TAG, "display registration failed");
    return g_ready ? g_display : nullptr;
}

bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 255 / 100), TAG, "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
