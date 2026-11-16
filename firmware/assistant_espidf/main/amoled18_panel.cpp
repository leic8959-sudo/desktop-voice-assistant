#include "amoled216_panel.h"
#include "amoled216_board.h"
#include "sh8601_18_commands.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

namespace {
constexpr const char *kTag = "amoled18_panel";
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
bool g_attempted, g_ready;

esp_err_t initialize() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = GPIO_NUM_11;
    bus.data0_io_num = GPIO_NUM_4; bus.data1_io_num = GPIO_NUM_5;
    bus.data2_io_num = GPIO_NUM_6; bus.data3_io_num = GPIO_NUM_7;
    bus.data4_io_num = GPIO_NUM_NC; bus.data5_io_num = GPIO_NUM_NC;
    bus.data6_io_num = GPIO_NUM_NC; bus.data7_io_num = GPIO_NUM_NC;
    bus.flags = SPICOMMON_BUSFLAG_QUAD; bus.max_transfer_sz = 368 * 20 * 2;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "QSPI2");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_12; io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0; io.pclk_hz = 40000000; io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32; io.lcd_param_bits = 8; io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_io), kTag, "32-bit QSPI IO");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = amoled_board::panel_reset;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; panel.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(sh8601_rgb565_create(g_io, &panel, {368, 448, 0, 0}, SH8601_18_COMMANDS,
        sizeof(SH8601_18_COMMANDS) / sizeof(Sh8601Command), &g_panel), kTag, "SH8601 RGB565");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), kTag, "SH8601 reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), kTag, "SH8601 init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_brightness(g_panel, 0), kTag, "SH8601 dark");
    return esp_lcd_panel_disp_on_off(g_panel, true);
}
}

esp_err_t amoled216_panel_create(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
    if (!io || !panel) return ESP_ERR_INVALID_ARG;
    *io = nullptr; *panel = nullptr;
    if (!g_attempted) {
        g_attempted = true;
        const esp_err_t result = initialize();
        if (result != ESP_OK) {
            const esp_err_t dark = g_panel ? esp_lcd_panel_set_brightness(g_panel, 0) : ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag, "display stopped error=%s dark=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark));
            return result;
        }
        g_ready = true;
    }
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    *io = g_io; *panel = g_panel;
    return ESP_OK;
}
esp_err_t amoled216_panel_set_brightness(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_set_brightness(g_panel, percent);
}
