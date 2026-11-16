#include "sensair_board.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensair_panel_commands.h"

namespace {
constexpr const char *kTag = "sensair_board";
i2c_master_bus_handle_t g_bus;
i2c_master_dev_handle_t g_touch;
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
bool g_ready, g_display_attempted, g_wait_release = true, g_touch_failed;

esp_err_t initialize_panel(esp_lcd_panel_t *) {
    // Preserve the old ILI9341 adapter's preamble and the fixed board table.
    // Do not call the SDK default init, which adds a different RAMCTRL write.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x11, nullptr, 0), kTag, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x00, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x36, &axes, 1), kTag, "initial axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x3a, &format, 1), kTag, "initial format");
    for (const auto &command : SENSAIR_PANEL_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, command.code, command.data, command.length), kTag, "vendor command 0x%02x", command.code);
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}

esp_err_t create_panel() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_23; bus.miso_io_num = GPIO_NUM_NC;
    bus.sclk_io_num = GPIO_NUM_24; bus.quadwp_io_num = GPIO_NUM_NC; bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = 284 * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "display SPI2");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_25; io.dc_gpio_num = GPIO_NUM_26;
    io.spi_mode = 0; io.pclk_hz = 40000000; io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_io), kTag, "display IO");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_NC; panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    // Both drivers use the same 16-bit DCS drawing and MADCTL operations here.
    // The fixed table must match the SDK's initial RGB MADCTL cache (zero).
    static_assert(SENSAIR_PANEL_COMMANDS[1].code == 0x36 && SENSAIR_PANEL_COMMANDS[1].data[0] == 0x00);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_io, &panel, &g_panel), kTag, "SDK panel operations");
    g_panel->init = initialize_panel;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), kTag, "software reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), kTag, "vendor init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), kTag, "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 36, 0), kTag, "visible window");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), kTag, "mirrorY");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), kTag, "landscape axes");
    return esp_lcd_panel_disp_on_off(g_panel, true);
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0; bus.sda_io_num = GPIO_NUM_2; bus.scl_io_num = GPIO_NUM_3;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

esp_err_t sensair_board_init() {
    if (g_ready) return ESP_OK;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_1, 0), kTag, "PA off latch");
    gpio_config_t pa = {};
    pa.pin_bit_mask = 1ULL << GPIO_NUM_1; pa.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&pa), kTag, "PA readback pad");
    if (gpio_get_level(GPIO_NUM_1)) return ESP_ERR_INVALID_RESPONSE;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), kTag, "touch key I2C0");
    i2c_device_config_t touch = {};
    touch.dev_addr_length = I2C_ADDR_BIT_LEN_7; touch.device_address = 0x15; touch.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &touch, &g_touch), kTag, "CST816D touch key");
    g_ready = true;
    return ESP_OK;
}

ManualInputKey sensair_read_keys() {
    if (!g_ready) return ManualInputKey::Invalid;
    const uint8_t reg = 0x02;
    uint8_t packet[6]{};
    const esp_err_t result = i2c_master_transmit_receive(g_touch, &reg, 1, packet, sizeof(packet), 100);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(kTag, "touch key failed=%s", esp_err_to_name(result));
        g_touch_failed = true; g_wait_release = true;
        return ManualInputKey::Invalid;
    }
    g_touch_failed = false;
    const uint8_t count = packet[0] & 0x0f, event = packet[1] >> 6;
    if (count > 1 || event == 3) { g_wait_release = true; return ManualInputKey::Invalid; }
    const bool touch = count == 1 && event != 1;
    const bool boot = !gpio_get_level(GPIO_NUM_28);
    if (!touch && !boot) { g_wait_release = false; return ManualInputKey::None; }
    if (g_wait_release || (touch && boot)) { g_wait_release = true; return ManualInputKey::Invalid; }
    return ManualInputKey::Listen;
}

lv_display_t *bsp_display_start() {
    if (g_display_attempted) return g_display;
    g_display_attempted = true;
    if (sensair_board_init() != ESP_OK || create_panel() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 0; port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io; display.panel_handle = g_panel;
    display.hres = 284; display.vres = 240; display.buffer_size = 284 * 20;
    display.double_buffer = true; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.rotation.swap_xy = true; display.rotation.mirror_y = true;
    display.flags.buff_dma = true; display.flags.swap_bytes = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
    lvgl_port_unlock();
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_panel) return ESP_ERR_INVALID_STATE;
    if (percent != 0 && percent != 100) return ESP_ERR_NOT_SUPPORTED;
    return esp_lcd_panel_disp_on_off(g_panel, percent != 0); // Blanking, not PWM dimming.
}
