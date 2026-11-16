#include "board_profile.h"

#include <array>
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(ASSISTANT_BOARD_STOPWATCH)
#include "stopwatch_panel_commands.h"
#else
#include "waveshare_143c_commands.h"
#endif

namespace {
constexpr const char *TAG = "co5300_round_display";
constexpr int kSize = 466;
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_io_handle_t g_panel_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_started, g_ready, g_wait_for_release = true;
esp_err_t g_touch_error = ESP_OK;

void round_area(lv_area_t *area) {
    area->x1 = (area->x1 / 2) * 2;
    area->y1 = (area->y1 / 2) * 2;
    area->x2 = (area->x2 / 2) * 2 + 1;
    area->y2 = (area->y2 / 2) * 2 + 1;
}

void cancel_touch(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    // Manufacturer CST820 packet: count02, event/Xhigh03, Xlow04,
    // Yhigh05,Ylow06. Generic CST816S coordinates clamp the count and omit
    // event bits; use its init/reset API but preserve raw count/event here.
    uint8_t packet[5]{};
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    if (result != ESP_OK) {
        if (result != g_touch_error) ESP_LOGE(TAG, "touch_read_failed error=%s", esp_err_to_name(result));
        g_touch_error = result;
        cancel_touch(pointer);
        return;
    }
    if (g_touch_error != ESP_OK) ESP_LOGI(TAG, "touch recovered; wait for release");
    g_touch_error = ESP_OK;
    const uint8_t count = packet[0], event = packet[1] >> 6;
    if (!count || (count == 1 && event == 1)) { g_wait_for_release = false; return; }
    const uint16_t x = static_cast<uint16_t>((packet[1] & 0x0f) << 8) | packet[2];
    const uint16_t y = static_cast<uint16_t>((packet[3] & 0x0f) << 8) | packet[4];
    if (count != 1 || (event != 0 && event != 2) || x >= kSize || y >= kSize) {
        cancel_touch(pointer);
        return;
    }
    if (g_wait_for_release) return;
#if defined(ASSISTANT_BOARD_STOPWATCH)
    data->point.x = x;
    data->point.y = y;
#else
    // Match the manufacturer's180-degree orientation with zero-based ends.
    data->point.x = kSize - 1 - x;
    data->point.y = kSize - 1 - y;
#endif
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize_touch() {
#if !defined(ASSISTANT_BOARD_STOPWATCH)
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_16, 1), TAG, "CST820 reset high latch");
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << GPIO_NUM_16;
    reset.mode = GPIO_MODE_OUTPUT;
    reset.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "CST820 reset output");
    vTaskDelay(pdMS_TO_TICKS(200));
#endif
    // The cached family macro orders C designators differently from IDF5.5's
    // C++ struct. Assign the same protocol fields explicitly at this boundary.
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    io.scl_speed_hz = 400000;
    io.control_phase_bytes = 1;
    io.lcd_cmd_bits = 8;
    io.flags.disable_control_phase = 1;
    io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), TAG, "CST820 address15 IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kSize;
    touch.y_max = kSize;
#if defined(ASSISTANT_BOARD_STOPWATCH)
    // M5IOE1IO4 reset completed before display; do not toggle ESP GPIO16,
    // which is the microphone DIN on Stopwatch.
    touch.rst_gpio_num = GPIO_NUM_NC;
    touch.int_gpio_num = GPIO_NUM_13;
#else
    touch.rst_gpio_num = GPIO_NUM_16;
    touch.int_gpio_num = GPIO_NUM_17;
#endif
    // Reset is external on Stopwatch and owned by the family driver on1.43C.
    // The family init readsA7 without a chip-ID whitelist; no second transform.
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(g_touch_io, &touch, &g_touch), TAG, "CST820 family init/reset");
    return gpio_intr_disable(touch.int_gpio_num);
}

esp_err_t initialize_display() {
    spi_bus_config_t bus = {};
#if defined(ASSISTANT_BOARD_STOPWATCH)
    bus.sclk_io_num = GPIO_NUM_40;
    bus.data0_io_num = GPIO_NUM_41;
    bus.data1_io_num = GPIO_NUM_42;
    bus.data2_io_num = GPIO_NUM_46;
    bus.data3_io_num = GPIO_NUM_45;
#else
    bus.sclk_io_num = GPIO_NUM_14;
    bus.data0_io_num = GPIO_NUM_9;
    bus.data1_io_num = GPIO_NUM_10;
    bus.data2_io_num = GPIO_NUM_11;
    bus.data3_io_num = GPIO_NUM_12;
#endif
    bus.max_transfer_sz = kSize * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "CO5300 QSPI2");
    esp_lcd_panel_io_spi_config_t io = {};
#if defined(ASSISTANT_BOARD_STOPWATCH)
    io.cs_gpio_num = GPIO_NUM_39;
#else
    io.cs_gpio_num = GPIO_NUM_15;
#endif
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0;
    io.pclk_hz = 40000000;
    io.trans_queue_depth =
#if defined(ASSISTANT_BOARD_STOPWATCH)
        10;
#else
        8;
#endif
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), TAG, "CO5300 IO");
    static std::array<co5300_lcd_init_cmd_t, sizeof(WAVESHARE_143C_COMMANDS) / sizeof(WAVESHARE_143C_COMMANDS[0])> commands{};
    for (size_t index = 0; index < commands.size(); ++index) {
        const auto &command = WAVESHARE_143C_COMMANDS[index];
        commands[index] = {command.code, command.data, command.length, command.delay_ms};
    }
    co5300_vendor_config_t vendor = {};
    vendor.init_cmds = commands.data();
    vendor.init_cmds_size = commands.size();
    vendor.flags.use_qspi_interface = true;
    esp_lcd_panel_dev_config_t panel = {};
#if defined(ASSISTANT_BOARD_STOPWATCH)
    panel.reset_gpio_num = GPIO_NUM_NC;
#else
    panel.reset_gpio_num = GPIO_NUM_13;
#endif
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    panel.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(g_panel_io, &panel, &g_panel), TAG, "manufacturer CO5300");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "CO5300 reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "CO5300 vendor commands");
#if defined(ASSISTANT_BOARD_STOPWATCH)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 6, 0), TAG, "Stopwatch runtime gap6");
#else
    // Both fixed source and manufacturer runtime code use gap8, although
    // their init CASET begins at6. Preserve runtime behavior; alignment is
    // still a hardware acceptance item, not an inferred correction to6.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 8, 0), TAG, "manufacturer runtime gap8");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(g_panel, 0), TAG, "AMOLED initially dark");
#if defined(ASSISTANT_BOARD_STOPWATCH)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "Stopwatch native orientation");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, true), TAG, "MADCTL C0 orientation");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "CO5300 display on");
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
    if (bsp_i2c_init() != ESP_OK || initialize_display() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK || !lvgl_port_lock(1000)) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io;
    display.panel_handle = g_panel;
    display.hres = kSize;
    display.vres = kSize;
    display.buffer_size = kSize * 20;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true;
    display.flags.swap_bytes = true;
#if !defined(ASSISTANT_BOARD_STOPWATCH)
    display.rotation.mirror_x = true;
    display.rotation.mirror_y = true;
#endif
    display.rounder_cb = round_area;
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
    if (!g_ready) ESP_LOGE(TAG, "CO5300/touch registration failed");
    return g_ready ? g_display : nullptr;
}

bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_panel || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    return esp_lcd_panel_co5300_set_brightness(g_panel, static_cast<uint8_t>(percent));
}
