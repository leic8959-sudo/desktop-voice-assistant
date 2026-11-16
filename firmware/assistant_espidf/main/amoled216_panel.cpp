// Panel command data adapted from Waveshare BSP2.0.1, Apache-2.0.
// Fixed source and license are recorded in third_party/waveshare_amoled216/README.md.
#include "amoled216_panel.h"
#include "amoled216_board.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_log.h"

namespace {
constexpr const char *kTag = "amoled216_panel";
bool g_attempted, g_ready;
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;

// Persistent command payloads; only brightness51 differs from the BSP's FF.
constexpr uint8_t k20[] = {0x20}, k10[] = {0x10}, kA0[] = {0xa0}, k00[] = {0};
constexpr uint8_t k80[] = {0x80}, k55[] = {0x55}, kFF[] = {0xff};
#if defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)
constexpr uint8_t kColumn[] = {0, 6, 1, 0xd7}, kRow[] = {0, 0, 1, 0xd1};
const co5300_lcd_init_cmd_t kCommands[] = {
    {0xfe, k20, 1, 0},
    {0x19, k10, 1, 0},
    {0x1c, kA0, 1, 0},
    {0xfe, k00, 1, 0},
    {0xc4, k80, 1, 0},
    {0x3a, k55, 1, 0},
    {0x35, k00, 1, 0},
    {0x53, k20, 1, 0},
    {0x51, k00, 1, 0},
    {0x63, kFF, 1, 0},
    {0x2a, kColumn, 4, 0},
    {0x2b, kRow, 4, 600},
    {0x11, nullptr, 0, 600},
    {0x29, nullptr, 0, 0},
};
#else
constexpr uint8_t kWindow[] = {0, 0, 1, 0xdf};
const co5300_lcd_init_cmd_t kCommands[] = {
    {0x11, nullptr, 0, 600},
    {0xfe, k20, 1, 0},
    {0x19, k10, 1, 0},
    {0x1c, kA0, 1, 0},
    {0xfe, k00, 1, 0},
    {0xc4, k80, 1, 0},
    {0x3a, k55, 1, 0},
    {0x35, k00, 1, 0},
    {0x53, k20, 1, 0},
    {0x51, k00, 1, 0},
    {0x63, kFF, 1, 0},
    {0x2a, kWindow, 4, 0},
    {0x2b, kWindow, 4, 0},
    {0x36, kA0, 1, 0},
    {0x29, nullptr, 0, 600},
};
#endif

esp_err_t initialize_panel() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = GPIO_NUM_38;
    bus.data0_io_num = GPIO_NUM_4; bus.data1_io_num = GPIO_NUM_5;
    bus.data2_io_num = GPIO_NUM_6; bus.data3_io_num = GPIO_NUM_7;
    bus.data4_io_num = GPIO_NUM_NC; bus.data5_io_num = GPIO_NUM_NC;
    bus.data6_io_num = GPIO_NUM_NC; bus.data7_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = amoled_board::width * 20 * sizeof(uint16_t);
    bus.flags = SPICOMMON_BUSFLAG_QUAD;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "QSPI2");
    // Same 32-bit02/32 command and color framing as the fixed BSP macro.
    esp_lcd_panel_io_spi_config_t io = CO5300_PANEL_IO_QSPI_CONFIG(GPIO_NUM_12, nullptr, nullptr);
    io.trans_queue_depth = 3;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_io), kTag, "panel IO");
    co5300_vendor_config_t vendor = {};
    vendor.init_cmds = kCommands; vendor.init_cmds_size = sizeof(kCommands) / sizeof(kCommands[0]);
    vendor.flags.use_qspi_interface = true;
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = amoled_board::panel_reset; panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16; panel.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(g_io, &panel, &g_panel), kTag, "CO5300");
#if defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 6, 0), kTag, "466-pixel column offset");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), kTag, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), kTag, "panel commands");
    // Keep MADCTL A0 from the original table. Generic mirror(false,false) or
    // swap(false) here would overwrite the board-specific orientation.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(g_panel, 0), kTag, "panel dark");
    return esp_lcd_panel_disp_on_off(g_panel, true);
}
}

esp_err_t amoled216_panel_create(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel) {
    if (!io || !panel) return ESP_ERR_INVALID_ARG;
    *io = nullptr; *panel = nullptr;
    if (!g_attempted) {
        g_attempted = true;
        const esp_err_t result = initialize_panel();
        if (result != ESP_OK) {
            const esp_err_t dark = g_panel ? esp_lcd_panel_co5300_set_brightness(g_panel, 0) : ESP_ERR_INVALID_STATE;
            ESP_LOGE(kTag, "panel stopped error=%s dark=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark));
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
    return esp_lcd_panel_co5300_set_brightness(g_panel, percent);
}
