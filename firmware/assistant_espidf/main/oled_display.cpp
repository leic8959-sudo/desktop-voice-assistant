#include "board_profile.h"

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "oled_display";
i2c_master_bus_handle_t g_i2c;
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
i2c_master_bus_handle_t g_codec_i2c;
#endif
#if defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_EDA_ROBOT)
constexpr bool kMirror = false;
#else
constexpr bool kMirror = true;
#endif
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
bool g_started, g_on;
int g_percent = -1;

esp_err_t initialize_panel() {
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "OLED I2C");
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = 0x3c;
    io.scl_speed_hz = 400000;
    io.control_phase_bytes = 1;
    io.dc_bit_offset = 6;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.transaction_timeout_ms = 1000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_i2c, &io, &g_io), TAG, "OLED panel IO");
    esp_lcd_panel_ssd1306_config_t vendor = {};
    vendor.height = board_profile::height;
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_NC;
    panel.bits_per_pixel = 1;
    panel.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(g_io, &panel, &g_panel), TAG, "SSD1306 factory");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "SSD1306 reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "SSD1306 init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, false), TAG, "SSD1306 invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, kMirror, kMirror), TAG, "SSD1306 mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "SSD1306 on");
    g_on = true;
    return ESP_OK;
}
}

esp_err_t bsp_i2c_init() {
    if (g_i2c) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
    bus.sda_io_num = GPIO_NUM_7;
    bus.scl_io_num = GPIO_NUM_8;
#elif defined(ASSISTANT_BOARD_EDA_ROBOT)
    bus.sda_io_num = GPIO_NUM_12;
    bus.scl_io_num = GPIO_NUM_13;
#elif defined(ASSISTANT_BOARD_WK)
    bus.sda_io_num = GPIO_NUM_8;
    bus.scl_io_num = GPIO_NUM_9;
#else
    bus.sda_io_num = GPIO_NUM_41;
    bus.scl_io_num = GPIO_NUM_42;
#endif
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_i2c);
}

i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }

#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
esp_err_t bsp_codec_i2c_init() {
    if (g_codec_i2c) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1;
    bus.sda_io_num = GPIO_NUM_42;
    bus.scl_io_num = GPIO_NUM_41;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_codec_i2c);
}
i2c_master_bus_handle_t bsp_codec_i2c_get_handle() { return g_codec_i2c; }
#endif

lv_display_t *bsp_display_start() {
    if (g_started) return g_display;
    g_started = true;
    if (initialize_panel() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io;
    display.panel_handle = g_panel;
    display.hres = board_profile::width;
    display.vres = board_profile::height;
    display.buffer_size = board_profile::width * board_profile::height;
    // esp_lvgl_port owns its supported RGB565-to-SSD1306 page conversion.
    // A full frame is only8/16KB; this avoids a parallel hand-written bit packer.
    display.monochrome = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.rotation.mirror_x = kMirror;
    display.rotation.mirror_y = kMirror;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
    lvgl_port_unlock();
    if (!g_display) ESP_LOGE(TAG, "SSD1306 LVGL registration failed");
    return g_display;
}

bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }

esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_panel || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    const bool on = percent != 0;
    if (on != g_on) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, on), TAG, "OLED on/off");
        g_on = on;
    }
    if (on && percent != g_percent) {
        const uint8_t contrast = percent * 255 / 100;
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x81, &contrast, 1), TAG, "OLED contrast");
    }
    g_percent = percent;
    return ESP_OK;
}
