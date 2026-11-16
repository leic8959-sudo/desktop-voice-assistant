#include "sensecap_board.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "spd2010_qspi_panel.h"

namespace {
constexpr const char *TAG = "sensecap_display";
lv_display_t *g_display;
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
bool g_started, g_pwm_ready, g_ready;
void round_area(lv_area_t *area) {
    area->x1 = (area->x1 / 4) * 4;
    area->x2 = (area->x2 / 4) * 4 + 3;
}
}

lv_display_t *bsp_display_start() {
    if (g_started) return g_ready ? g_display : nullptr;
    g_started = true;
    if (sensecap_board_init() != ESP_OK) return nullptr;
    spi_bus_config_t bus = {};
    bus.sclk_io_num = GPIO_NUM_7;
    bus.data0_io_num = GPIO_NUM_9; bus.data1_io_num = GPIO_NUM_1;
    bus.data2_io_num = GPIO_NUM_14; bus.data3_io_num = GPIO_NUM_13;
    bus.max_transfer_sz = 412 * 20 * sizeof(uint16_t);
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return nullptr;
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_45; io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 3; io.pclk_hz = 40000000; io.trans_queue_depth = 2;
    io.lcd_cmd_bits = 32; io.lcd_param_bits = 8; io.flags.quad_mode = true;
    if (esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &g_io) != ESP_OK) return nullptr;
    if (spd2010_qspi_panel_create(g_io, &g_panel) != ESP_OK ||
        esp_lcd_panel_reset(g_panel) != ESP_OK || esp_lcd_panel_init(g_panel) != ESP_OK ||
        esp_lcd_panel_mirror(g_panel, false, false) != ESP_OK ||
        esp_lcd_panel_disp_on_off(g_panel, true) != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io; display.panel_handle = g_panel;
    display.hres = 412; display.vres = 412; display.buffer_size = 412 * 20;
    display.double_buffer = true; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true; display.flags.swap_bytes = true; display.rounder_cb = round_area;
    // SPD2010 rejects swap_xy evenfalse. At rotation0, software rotation
    // skips the port's unsupported hardware callback and keeps native axes.
    display.flags.sw_rotate = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
    lvgl_port_unlock();
    if (!g_display) return nullptr;
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0; timer.freq_hz = 5000; timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK) return nullptr;
    ledc_channel_config_t channel = {};
    channel.gpio_num = GPIO_NUM_8; channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0; channel.timer_sel = LEDC_TIMER_0; channel.duty = 0;
    if (ledc_channel_config(&channel) != ESP_OK) return nullptr;
    g_pwm_ready = true;
    g_ready = true;
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_pwm_ready) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), TAG, "backlight PWM duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
