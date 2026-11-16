#include "board_profile.h"

#include "esp_check.h"
#include "esp_log.h"
#include "hub75.h"

namespace {
constexpr const char *TAG = "matrix_display";
i2c_master_bus_handle_t g_i2c;
lv_display_t *g_display;
uint16_t *g_pixels;
bool g_started, g_failed;

Hub75Config panel_config() {
    Hub75Config config{};
    config.panel_width = 64; config.panel_height = 64;
    config.layout_cols = 1; config.layout_rows = 1;
    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;
    config.shift_driver = Hub75ShiftDriver::FM6126A;
    config.min_refresh_rate = 240;
    config.output_clock_speed = Hub75ClockSpeed::HZ_20M;
    config.double_buffer = false;
    config.brightness = 0;
    config.pins.r1 = 4; config.pins.g1 = 5; config.pins.b1 = 6;
    config.pins.r2 = 7; config.pins.g2 = 15; config.pins.b2 = 16;
    config.pins.a = 18; config.pins.b = 8; config.pins.c = 3;
    config.pins.d = 42; config.pins.e = 9;
    config.pins.clk = 41; config.pins.lat = 40; config.pins.oe = 2;
    return config;
}
// Persistent lifetime also covers a partially initialized external driver.
Hub75Driver g_panel(panel_config());

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    if (!g_failed && g_panel.is_running()) {
        if (area->x1 != 0 || area->y1 != 0 || area->x2 != 63 || area->y2 != 63) {
            g_failed = true;
            const esp_err_t blank = matrix_display_quiesce();
            ESP_LOGE(TAG, "unexpected partial frame; presentation stopped blank=%s", esp_err_to_name(blank));
        } else {
            // Public single-buffer API consumes RGB565 synchronously. LVGL
            // completion means its buffer is reusable, not physical acceptance.
            g_panel.draw_pixels(0, 0, 64, 64, pixels, Hub75PixelFormat::RGB565, Hub75ColorOrder::RGB, false);
        }
    }
    lv_display_flush_ready(display);
}
}

esp_err_t matrix_display_quiesce() {
    g_failed = true;
    // Detach the LCD_CAM output route before holding the active-low OE high.
    ESP_RETURN_ON_ERROR(gpio_reset_pin(GPIO_NUM_2), TAG, "OE detach");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_2, 1), TAG, "OE blank latch");
    return gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
}

esp_err_t bsp_i2c_init() {
    if (g_i2c) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0; bus.sda_io_num = GPIO_NUM_47; bus.scl_io_num = GPIO_NUM_48;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_i2c);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }

lv_display_t *bsp_display_start() {
    if (g_started) return g_display;
    g_started = true;
    if (bsp_i2c_init() != ESP_OK) return nullptr;
    g_pixels = static_cast<uint16_t *>(heap_caps_malloc(64 * 64 * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_pixels) return nullptr;
    if (!g_panel.begin()) {
        const esp_err_t blank = matrix_display_quiesce();
        ESP_LOGE(TAG, "HUB75 initialization failed blank=%s;restart required", esp_err_to_name(blank));
        return nullptr;
    }
    g_panel.clear();
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK || !lvgl_port_lock(1000)) return nullptr;
    g_display = lv_display_create(64, 64);
    if (g_display) {
        lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(g_display, g_pixels, nullptr, 64 * 64 * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(g_display, flush);
    }
    lvgl_port_unlock();
    ESP_LOGI(TAG, "HUB75 configured64x64 singlebuffer;GDMA/LCD_CAM display,I2S0 audio;physicalscanunaccepted");
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (g_failed || !g_panel.is_running()) return ESP_ERR_INVALID_STATE;
    g_panel.set_brightness(static_cast<uint8_t>((percent * 255 + 50) / 100));
    return ESP_OK;
}
