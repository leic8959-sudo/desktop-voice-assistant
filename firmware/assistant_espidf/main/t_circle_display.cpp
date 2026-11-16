#include "t_circle_board.h"

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "t_circle_gc9d01n_init.h"

namespace {
constexpr const char *TAG = "t_circle_display";
constexpr int kSize = 160;
struct CirclePanel {
    esp_lcd_panel_t interface{};
    esp_lcd_panel_io_handle_t io = nullptr;
    uint8_t orientation = 0;
    int x_gap = 0, y_gap = 0;
};
CirclePanel g_panel;
i2c_master_bus_handle_t g_i2c;
i2c_master_dev_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_touch_failed;
bool g_pwm_ready;

esp_err_t panel_command(uint8_t command, const void *data = nullptr, size_t length = 0) {
    return esp_lcd_panel_io_tx_param(g_panel.io, command, data, length);
}
esp_err_t panel_delete(esp_lcd_panel_t *) { return ESP_OK; }
esp_err_t panel_reset(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(panel_command(0x01), TAG, "panel software reset");
    vTaskDelay(pdMS_TO_TICKS(150));
    return ESP_OK;
}
esp_err_t panel_init(esp_lcd_panel_t *) {
    for (const auto &command : GC9D01N_INIT) {
        ESP_RETURN_ON_ERROR(panel_command(command.code, command.data, command.length), TAG, "panel initialization");
        if (command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    g_panel.orientation = 0;
    return ESP_OK;
}
esp_err_t panel_draw(esp_lcd_panel_t *, int x_start, int y_start, int x_end, int y_end, const void *pixels) {
    if (!pixels || x_start < 0 || y_start < 0 || x_end > kSize || y_end > kSize || x_end <= x_start || y_end <= y_start) return ESP_ERR_INVALID_ARG;
    const size_t bytes = (x_end - x_start) * (y_end - y_start) * sizeof(uint16_t);
    x_start += g_panel.x_gap; x_end += g_panel.x_gap;
    y_start += g_panel.y_gap; y_end += g_panel.y_gap;
    const uint8_t columns[] = {static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start), static_cast<uint8_t>((x_end - 1) >> 8), static_cast<uint8_t>(x_end - 1)};
    const uint8_t rows[] = {static_cast<uint8_t>(y_start >> 8), static_cast<uint8_t>(y_start), static_cast<uint8_t>((y_end - 1) >> 8), static_cast<uint8_t>(y_end - 1)};
    ESP_RETURN_ON_ERROR(panel_command(0x2A, columns, sizeof(columns)), TAG, "panel columns");
    ESP_RETURN_ON_ERROR(panel_command(0x2B, rows, sizeof(rows)), TAG, "panel rows");
    return esp_lcd_panel_io_tx_color(g_panel.io, 0x2C, pixels, bytes);
}
esp_err_t panel_mirror(esp_lcd_panel_t *, bool x, bool y) {
    g_panel.orientation = (g_panel.orientation & ~0xC0) | (x ? 0x40 : 0) | (y ? 0x80 : 0);
    return panel_command(0x36, &g_panel.orientation, 1);
}
esp_err_t panel_swap(esp_lcd_panel_t *, bool swap) {
    g_panel.orientation = (g_panel.orientation & ~0x20) | (swap ? 0x20 : 0);
    return panel_command(0x36, &g_panel.orientation, 1);
}
esp_err_t panel_gap(esp_lcd_panel_t *, int x, int y) {
    if (x < 0 || y < 0 || x + kSize > 256 || y + kSize > 256) return ESP_ERR_INVALID_ARG;
    g_panel.x_gap = x; g_panel.y_gap = y;
    return ESP_OK;
}
esp_err_t panel_invert(esp_lcd_panel_t *, bool invert) { return panel_command(invert ? 0x21 : 0x20); }
esp_err_t panel_on(esp_lcd_panel_t *, bool on) { return panel_command(on ? 0x29 : 0x28); }

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    const uint8_t first_register = 0x02;
    uint8_t registers[6]{};
    const esp_err_t result = i2c_master_transmit_receive(g_touch, &first_register, 1, registers, sizeof(registers), 20);
    if (result != ESP_OK) {
        lv_indev_reset(pointer, nullptr);
        // Release on a failed poll so a disconnected touch controller cannot
        // leave an action pressed. Log transitions, not every 30ms poll.
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_read failed: %s", esp_err_to_name(result));
        g_touch_failed = true;
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch_read recovered");
    g_touch_failed = false;
    if (!(registers[0] & 0x0F)) return;
    const int x = ((registers[1] & 0x0F) << 8) | registers[2];
    const int y = ((registers[3] & 0x0F) << 8) | registers[4];
    if (x >= kSize || y >= kSize) { lv_indev_reset(pointer, nullptr); return; }
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize_peripherals() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = GPIO_NUM_18;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    backlight.duty = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight channel");
    g_pwm_ready = true;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_11;
    bus.scl_io_num = GPIO_NUM_14;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_i2c), TAG, "touch I2C bus");
    i2c_device_config_t touch = {};
    touch.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    touch.device_address = 0x15;
    touch.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &touch, &g_touch), TAG, "touch device");
    spi_bus_config_t spi = {};
    spi.mosi_io_num = GPIO_NUM_17;
    spi.miso_io_num = GPIO_NUM_NC;
    spi.sclk_io_num = GPIO_NUM_15;
    spi.quadwp_io_num = GPIO_NUM_NC;
    spi.quadhd_io_num = GPIO_NUM_NC;
    spi.max_transfer_sz = kSize * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &spi, SPI_DMA_CH_AUTO), TAG, "display SPI");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_13;
    io.dc_gpio_num = GPIO_NUM_16;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &g_panel.io), TAG, "display IO");
    g_panel.interface.del = panel_delete;
    g_panel.interface.reset = panel_reset;
    g_panel.interface.init = panel_init;
    g_panel.interface.draw_bitmap = panel_draw;
    g_panel.interface.mirror = panel_mirror;
    g_panel.interface.swap_xy = panel_swap;
    g_panel.interface.set_gap = panel_gap;
    g_panel.interface.invert_color = panel_invert;
    g_panel.interface.disp_on_off = panel_on;
    ESP_RETURN_ON_ERROR(panel_reset(&g_panel.interface), TAG, "display reset");
    ESP_RETURN_ON_ERROR(panel_init(&g_panel.interface), TAG, "display init");
    return panel_invert(&g_panel.interface, false);
}
}

lv_display_t *bsp_display_start() {
    if (g_display) return g_pointer ? g_display : nullptr;
    // Firmware initialization is single-shot; a failed init is reported to the
    // caller rather than retrying over partially owned peripheral resources.
    if (g_pwm_ready || initialize_peripherals() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel.io;
    display.panel_handle = &g_panel.interface;
    display.buffer_size = kSize * 20;
    display.double_buffer = true;
    display.hres = kSize;
    display.vres = kSize;
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
    lvgl_port_unlock();
    if (!g_display || !g_pointer) {
        ESP_LOGE(TAG, "LVGL display/touch allocation failed");
        return nullptr;
    }
    return g_display;
}

bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), TAG, "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
