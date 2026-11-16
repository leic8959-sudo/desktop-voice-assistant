#include "board_profile.h"

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#if !defined(ASSISTANT_BOARD_LCD_EV_2)
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_touch_ft5x06.h"
#include "lcd_ev_panel_commands.h"
#endif
#include "esp_log.h"

namespace {
constexpr const char *TAG = "lcd_ev_display";
constexpr int kWidth = spi_board::width, kHeight = spi_board::height;
#if !defined(ASSISTANT_BOARD_LCD_EV_2)
static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS >= 2, "LCD EV touch must reject multiple contacts");
#endif
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_handle_t g_panel;
esp_lcd_panel_io_handle_t g_touch_io;
#if !defined(ASSISTANT_BOARD_LCD_EV_2)
esp_lcd_touch_handle_t g_touch;
#endif
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_started, g_ready, g_wait_for_release = true, g_touch_failed;

void cancel_press(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
#if defined(ASSISTANT_BOARD_LCD_EV_2)
    uint8_t raw_status = 0;
    esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x814e, &raw_status, 1);
    uint8_t count = raw_status & 0x0f;
    uint8_t packet[11]{};
    // PublicGT1151 protocol:status+one8bytepoint+key+checksum. The component
    // drops report-clear errors; use the board-owned IO with a bounded packet
    // and checked clear so failed acknowledgement never becomes a fresh click.
    if (result == ESP_OK && count == 1) {
        result = esp_lcd_panel_io_rx_param(g_touch_io, 0x814e, packet, sizeof(packet));
        uint8_t checksum = 0;
        for (const uint8_t value : packet) checksum += value;
        if (result == ESP_OK && checksum) result = ESP_ERR_INVALID_CRC;
        if (result == ESP_OK && (packet[0] & 0x0f) != 1) count = 2;
    }
    const uint8_t clear = 0;
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(g_touch_io, 0x814e, &clear, 1);
    const uint16_t x = static_cast<uint16_t>(packet[3]) << 8 | packet[2];
    const uint16_t y = static_cast<uint16_t>(packet[5]) << 8 | packet[4];
    const bool invalid_report = count > 1;
#else
    // The installedFT5x06 coordinate getter drops raw event bits. Reuse its
    // initializedIO and read the contiguous status/first record explicitly.
    uint8_t packet[7]{};
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    uint8_t count = packet[0] & 0x0f;
    const uint8_t event = packet[1] >> 6;
    const uint16_t x = static_cast<uint16_t>((packet[1] & 0x0f) << 8) | packet[2];
    const uint16_t y = static_cast<uint16_t>((packet[3] & 0x0f) << 8) | packet[4];
    if (count == 1 && event == 1) count = 0;
    const bool invalid_report = count > 1 || (count == 1 && event != 0 && event != 2);
#endif
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_failed error=%s", esp_err_to_name(result));
        g_touch_failed = true;
        cancel_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch recovered; wait for release");
    g_touch_failed = false;
    if (invalid_report) {
        cancel_press(pointer);
        return;
    }
    if (!count) { g_wait_for_release = false; return; }
    if (x >= kWidth || y >= kHeight) {
        cancel_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

#if !defined(ASSISTANT_BOARD_LCD_EV_2)
esp_err_t initialize_gc9503() {
    // EspressifSUB2 requires VSYNC high before GC9503 startup. The RGB driver
    // takes this pin afterwards. SUB3/ST7262 must not receive this sequence.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_3, 1), TAG, "SUB2 VSYNC high latch");
    gpio_config_t vsync = {};
    vsync.pin_bit_mask = 1ULL << GPIO_NUM_3;
    vsync.mode = GPIO_MODE_OUTPUT;
    vsync.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&vsync), TAG, "SUB2 VSYNC output");
    const auto expander = lcd_ev_get_expander();
    if (!expander) return ESP_ERR_INVALID_STATE;
    esp_lcd_panel_io_3wire_spi_config_t io = {};
    io.line_config.cs_io_type = IO_TYPE_EXPANDER;
    io.line_config.cs_expander_pin = IO_EXPANDER_PIN_NUM_1;
    io.line_config.scl_io_type = IO_TYPE_EXPANDER;
    io.line_config.scl_expander_pin = IO_EXPANDER_PIN_NUM_2;
    io.line_config.sda_io_type = IO_TYPE_EXPANDER;
    io.line_config.sda_expander_pin = IO_EXPANDER_PIN_NUM_3;
    io.line_config.io_expander = expander;
    io.expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX;
    io.lcd_cmd_bytes = 1;
    io.lcd_param_bytes = 1;
    io.flags.use_dc_bit = true;
    io.flags.del_keep_cs_inactive = true;
    esp_lcd_panel_io_handle_t initialization_io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_3wire_spi(&io, &initialization_io), TAG, "GC9503 IO mode0 rising edge");
    esp_err_t result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_SWRESET, nullptr, 0);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(120));
    // Fixed driver's RGB/default B1=10 and18-bit controller COLMOD60 are
    // separate from the RGB56516-bit framebuffer/bus contract.
    const uint8_t orientation = 0x10, format = 0x60;
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, 0xb1, &orientation, 1);
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(initialization_io, LCD_CMD_COLMOD, &format, 1);
    for (const auto &command : LCD_EV_COMMANDS) {
        if (result != ESP_OK) break;
        result = esp_lcd_panel_io_tx_param(initialization_io, command.code, command.length ? command.data : nullptr, command.length);
        if (result != ESP_OK) ESP_LOGE(TAG, "GC9503 command=0x%02x error=%s", command.code, esp_err_to_name(result));
        if (result == ESP_OK && command.delay_ms) vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    const esp_err_t cleanup = esp_lcd_panel_io_del(initialization_io);
    if (cleanup != ESP_OK) ESP_LOGE(TAG, "GC9503 IO cleanup failed: %s", esp_err_to_name(cleanup));
    return result != ESP_OK ? result : cleanup;
}
#endif

esp_err_t initialize_rgb() {
#if !defined(ASSISTANT_BOARD_LCD_EV_2)
    ESP_RETURN_ON_ERROR(initialize_gc9503(), TAG, "SUB2 controller startup");
#endif
    esp_lcd_rgb_panel_config_t panel = {};
    panel.clk_src = LCD_CLK_SRC_PLL160M;
    panel.timings.h_res = kWidth;
    panel.timings.v_res = kHeight;
#if defined(ASSISTANT_BOARD_LCD_EV_2)
    // ManufacturerSUB3/ST7262 timing, not the copied GC9503 table in v2.5.2.
    panel.timings.pclk_hz = 18000000;
    panel.timings.hsync_pulse_width = 40;
    panel.timings.hsync_back_porch = 40;
    panel.timings.hsync_front_porch = 48;
    panel.timings.vsync_pulse_width = 23;
    panel.timings.vsync_back_porch = 32;
    panel.timings.vsync_front_porch = 13;
    panel.timings.flags.pclk_active_neg = true;
#else
    panel.timings.pclk_hz = 16000000;
    panel.timings.hsync_pulse_width = 10;
    panel.timings.hsync_back_porch = 10;
    panel.timings.hsync_front_porch = 20;
    panel.timings.vsync_pulse_width = 10;
    panel.timings.vsync_back_porch = 10;
    panel.timings.vsync_front_porch = 10;
#endif
    panel.data_width = 16;
    panel.bits_per_pixel = 16;
    panel.num_fbs = 2;
    panel.bounce_buffer_size_px = kWidth * 10;
    panel.dma_burst_size = 64;
    panel.hsync_gpio_num = GPIO_NUM_46;
    panel.vsync_gpio_num = GPIO_NUM_3;
    panel.de_gpio_num = GPIO_NUM_17;
    panel.pclk_gpio_num = GPIO_NUM_9;
    panel.disp_gpio_num = GPIO_NUM_NC;
#if defined(ASSISTANT_BOARD_LCD_EV_14)
    constexpr int data_pins[] = {10, 11, 12, 13, 14, 21, 47, 48, 45, 38, 39, 40, 41, 42, 2, 1};
#else
    constexpr int data_pins[] = {10, 11, 12, 13, 14, 21, 8, 18, 45, 38, 39, 40, 41, 42, 2, 1};
#endif
    for (int index = 0; index < 16; ++index) panel.data_gpio_nums[index] = data_pins[index];
    panel.flags.fb_in_psram = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel, &g_panel), TAG, "RGB panel");
    return esp_lcd_panel_init(g_panel);
}

esp_err_t initialize_touch() {
    esp_lcd_panel_io_i2c_config_t io = {};
#if defined(ASSISTANT_BOARD_LCD_EV_2)
    io.dev_addr = 0x14;
    io.lcd_cmd_bits = 16;
#else
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    io.lcd_cmd_bits = 8;
#endif
    io.scl_speed_hz = 100000;
    io.control_phase_bytes = 1;
    io.flags.disable_control_phase = 1;
    io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), TAG, "touch IO");
#if defined(ASSISTANT_BOARD_LCD_EV_2)
    // Product-register contract from esp-bsp@6f82fd66a099cfd388cf9376ca1c5d6dcc98381e,
    // esp_lcd_touch_gt1151.c, Apache-2.0 (third_party/gt1151/LICENSE).
    // The fixed SUB3 has reset/INT NC, so the original constructor performs
    // no GPIO operation. Preserve its11-byte read and unusual nonzero-sum
    // check; this is not a newly inferred identity or checksum algorithm.
    uint8_t product[11]{};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(g_touch_io, 0x8140, product, sizeof(product)), TAG, "GT1151 product read");
    uint8_t sum = 0;
    for (const uint8_t value : product) sum += value;
    const auto uppercase_or_digit = [](uint8_t value) { return (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'); };
    if (!sum || !uppercase_or_digit(product[0]) || !uppercase_or_digit(product[1]) ||
        !uppercase_or_digit(product[2]) || product[10] == 0xff) {
        ESP_LOGE(TAG, "GT1151 product rejected sum=%u prefix=%02x:%02x:%02x sensor=%02x", sum, product[0], product[1], product[2], product[10]);
        return ESP_ERR_INVALID_CRC;
    }
    ESP_LOGI(TAG, "GT1151 product=%02x:%02x:%02x:%02x sensor=%02x;fixed14/noGPIOreset;physical identity unconfirmed",
        product[0], product[1], product[2], product[3], product[10]);
    return ESP_OK;
#else
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kWidth;
    touch.y_max = kHeight;
    // Both manufacturer subboards use no ESP reset/interrupt GPIO. Poll and
    // preserve their native coordinates; do not guess GPIOs from other boards.
    touch.rst_gpio_num = GPIO_NUM_NC;
    touch.int_gpio_num = GPIO_NUM_NC;
    return esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch);
#endif
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
    if (bsp_i2c_init() != ESP_OK || !lcd_ev_get_expander() || initialize_rgb() != ESP_OK || initialize_touch() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.panel_handle = g_panel;
    // RGB has no persistent panel IO; the SUB2 initialization IO was deleted.
    display.hres = kWidth;
    display.vres = kHeight;
    display.buffer_size = kWidth * kHeight;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.direct_mode = true;
    lvgl_port_display_rgb_cfg_t rgb = {};
    rgb.flags.bb_mode = true;
    rgb.flags.avoid_tearing = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp_rgb(&display, &rgb);
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
    if (!g_ready) ESP_LOGE(TAG, "RGB/touch registration failed");
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    // Official BSP has no adjustable backlight. GPIO19 in the pinned config
    // is an unused macro on USB D-, not evidence for a backlight control.
    return ESP_ERR_NOT_SUPPORTED;
}
