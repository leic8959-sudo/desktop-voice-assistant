#include "board_profile.h"

#include <array>
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcdwiki_panel.h"

namespace {
constexpr const char *TAG = "lcdwiki_display";
constexpr int kWidth = 320, kHeight = 480;
i2c_master_bus_handle_t g_bus;
i2c_master_dev_handle_t g_touch;
esp_lcd_panel_io_handle_t g_panel_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
lv_indev_t *g_pointer;
uint8_t g_max_points;
bool g_started, g_ready, g_pwm_ready, g_wait_for_release = true;
esp_err_t g_last_touch_error = ESP_OK;

esp_err_t touch_read(uint16_t reg, uint8_t *data, size_t length) {
    const uint8_t address[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    return i2c_master_transmit_receive(g_touch, address, sizeof(address), data, length, 100);
}

void cancel_press(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    uint8_t info = 0;
    esp_err_t result = touch_read(0x0010, &info, 1);
    std::array<uint8_t, 35> records{};
    if (result == ESP_OK && (info & 0x08)) {
        result = touch_read(0x0014, records.data(), 7 * g_max_points);
    }
    if (result != ESP_OK) {
        if (result != g_last_touch_error) ESP_LOGE(TAG, "touch_read failed: %s", esp_err_to_name(result));
        g_last_touch_error = result;
        cancel_press(pointer);
        return;
    }
    if (g_last_touch_error != ESP_OK) ESP_LOGI(TAG, "touch_read recovered; wait for release");
    g_last_touch_error = ESP_OK;
    int active = 0, x = 0, y = 0;
    for (uint8_t index = 0; index < g_max_points; ++index) {
        const uint8_t *point = records.data() + index * 7;
        if (!(point[0] & 0x80)) continue;
        ++active;
        x = ((point[0] & 0x3f) << 8) | point[1];
        y = ((point[2] & 0x3f) << 8) | point[3];
    }
    if (!active) { g_wait_for_release = false; return; }
    if (active != 1 || x >= kWidth || y >= kHeight) {
        cancel_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

void round_area(lv_area_t *area) {
    // ST77922 transfers start at a4-pixel boundary and span whole groups of4.
    area->x1 = (area->x1 / 4) * 4;
    area->x2 = (area->x2 / 4) * 4 + 3;
}

esp_err_t initialize_touch() {
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x55;
    device.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &device, &g_touch), TAG, "TDDI touch device");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, 0), TAG, "touch reset low latch");
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << GPIO_NUM_48;
    reset.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "touch reset output");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, 1), TAG, "touch reset release");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_config_t interrupt = {};
    interrupt.pin_bit_mask = 1ULL << GPIO_NUM_47;
    interrupt.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "touch INT input");
    ESP_RETURN_ON_ERROR(touch_read(0x0009, &g_max_points, 1), TAG, "touch point capacity");
    if (g_max_points < 1 || g_max_points > 5) {
        ESP_LOGE(TAG, "unsupported_touch_point_capacity=%u", g_max_points);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "TDDI touch ready capacity=%u address=0x55", g_max_points);
    return ESP_OK;
}

esp_err_t initialize_display() {
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "codec/touch I2C0");
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = GPIO_NUM_41;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight initially dark");
    g_pwm_ready = true;
    spi_bus_config_t bus = {};
    bus.sclk_io_num = GPIO_NUM_12;
    bus.data0_io_num = GPIO_NUM_11;
    bus.data1_io_num = GPIO_NUM_13;
    bus.data2_io_num = GPIO_NUM_14;
    bus.data3_io_num = GPIO_NUM_9;
    bus.max_transfer_sz = kWidth * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI2");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_10;
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0;
    io.pclk_hz = 80000000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), TAG, "ST77922 QSPI IO");
    ESP_RETURN_ON_ERROR(lcdwiki_panel_create(g_panel_io, &g_panel), TAG, "ST77922 fixed QSPI panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, false), TAG, "panel inversion");
    // The component rejects swap_xy even for false. The fixed panel stays in
    // its portrait axes; LVGL software rotation avoids that unsupported call.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "panel on");
    return initialize_touch();
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
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io;
    display.panel_handle = g_panel;
    display.hres = kWidth;
    display.vres = kHeight;
    display.buffer_size = kWidth * 20;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.rounder_cb = round_area;
    display.flags.buff_dma = true;
    display.flags.swap_bytes = true;
    display.flags.sw_rotate = true;
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
