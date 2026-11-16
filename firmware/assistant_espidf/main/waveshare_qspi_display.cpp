#include "waveshare_qspi_board.h"

#include <array>
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#if !defined(ASSISTANT_BOARD_LCD_185B) && !defined(ASSISTANT_BOARD_TAIJI) && !defined(ASSISTANT_BOARD_VOCAT) && !defined(ASSISTANT_BOARD_MOJI2_C5)
#include "esp_io_expander_tca9554.h"
#endif
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#if defined(ASSISTANT_BOARD_LCD_146)
#include "spd2010_qspi_panel.h"
#include "spd2010_touch.h"
#else
#if defined(ASSISTANT_BOARD_TAIJI)
#include "taiji_panel.h"
#else
#include "st77916_qspi_panel.h"
#endif
#if BSP_CAPS_TOUCH
#include "esp_lcd_touch_cst816s.h"
#endif
#if defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_MOJI2_C5)
#include "vocat_panel_commands.h"
#elif !defined(ASSISTANT_BOARD_TAIJI)
#include "st77916_board_init.h"
#include "st77916_default_commands.h"
#endif
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "waveshare_qspi";
#if defined(ASSISTANT_BOARD_LCD_146)
constexpr int kSize = 412;
#else
constexpr int kSize = 360;
#endif
i2c_master_bus_handle_t g_bus;
#if defined(ASSISTANT_BOARD_LCD_185)
i2c_master_bus_handle_t g_touch_bus;
#endif
#if !defined(ASSISTANT_BOARD_LCD_185B) && !defined(ASSISTANT_BOARD_TAIJI) && !defined(ASSISTANT_BOARD_VOCAT) && !defined(ASSISTANT_BOARD_MOJI2_C5)
esp_io_expander_handle_t g_expander;
#endif
esp_lcd_panel_io_handle_t g_panel_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
bool g_started, g_pwm_ready;
#if BSP_CAPS_TOUCH
esp_lcd_panel_io_handle_t g_touch_io;
#if defined(ASSISTANT_BOARD_LCD_146)
Spd2010Touch g_touch;
#else
esp_lcd_touch_handle_t g_touch;
#endif
lv_indev_t *g_pointer;
bool g_touch_failed;
#endif
#if defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_LCD_146)
bool g_wait_for_release = true;
#endif

void round_display_area(lv_area_t *area) {
#if defined(ASSISTANT_BOARD_LCD_146)
    // SPD2010 requires a start on a4-pixel boundary and a width divisible by4.
    area->x1 = (area->x1 / 4) * 4;
    area->x2 = (area->x2 / 4) * 4 + 3;
#else
    area->x1 = (area->x1 / 2) * 2;
    area->x2 = (area->x2 / 2) * 2 + 1;
#endif
}
#if BSP_CAPS_TOUCH
void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
#if defined(ASSISTANT_BOARD_LCD_146)
    Spd2010Frame frame{};
    const esp_err_t result = g_touch.Read(&frame);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "SPD touch read failed:%s", esp_err_to_name(result));
        g_touch_failed = true;
        g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "SPD touch recovered;wait for release");
    g_touch_failed = false;
    if (!frame.count) { g_wait_for_release = false; return; }
    if (frame.count != 1 || frame.points[0].x >= kSize || frame.points[0].y >= kSize) {
        g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = frame.points[0].x;
    data->point.y = frame.points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
#elif defined(ASSISTANT_BOARD_VOCAT)
    // CST816S1.1.2 clamps raw count toone and omits event bits. Reuse its
    // initializedIO with the fixedboard's raw02status/coordinate packet.
    uint8_t packet[6]{};
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    const uint8_t count = packet[0] & 0x0f, event = packet[1] >> 6;
    const uint16_t x = static_cast<uint16_t>((packet[1] & 0x0f) << 8) | packet[2];
    const uint16_t y = static_cast<uint16_t>((packet[3] & 0x0f) << 8) | packet[4];
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "VoCat touch read failed:%s", esp_err_to_name(result));
        g_touch_failed = true;
        g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "VoCat touch recovered;wait for release");
    g_touch_failed = false;
    if (!count || (count == 1 && event == 1)) { g_wait_for_release = false; return; }
    if (count != 1 || (event != 0 && event != 2) || x >= kSize || y >= kSize) {
        g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
#else
    esp_err_t result = esp_lcd_touch_read_data(g_touch);
    esp_lcd_touch_point_data_t point = {};
    uint8_t count = 0;
    if (result == ESP_OK) result = esp_lcd_touch_get_data(g_touch, &point, &count, 1);
    if (result != ESP_OK) {
        lv_indev_reset(pointer, nullptr);
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_read failed: %s", esp_err_to_name(result));
        g_touch_failed = true;
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch_read recovered");
    g_touch_failed = false;
    if (!count) return;
    if (point.x >= kSize || point.y >= kSize) { lv_indev_reset(pointer, nullptr); return; }
#if defined(ASSISTANT_BOARD_TAIJI)
    data->point.x = kSize - 1 - point.x; data->point.y = kSize - 1 - point.y;
#else
    data->point.x = point.x; data->point.y = point.y;
#endif
    data->state = LV_INDEV_STATE_PRESSED;
#endif
}
#endif
#if !defined(ASSISTANT_BOARD_LCD_146)
esp_err_t create_st77916_panel() {
#if defined(ASSISTANT_BOARD_VOCAT) || defined(ASSISTANT_BOARD_MOJI2_C5)
#if defined(ASSISTANT_BOARD_VOCAT)
    if (!vocat_board_ready()) return ESP_ERR_INVALID_STATE;
#endif
    esp_lcd_panel_io_spi_config_t io = {};
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    io.cs_gpio_num = GPIO_NUM_3;
#else
    io.cs_gpio_num = GPIO_NUM_14;
#endif
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), TAG, "ST77916 QSPI IO");
    static std::array<St77916QspiCommand, sizeof(VOCAT_PANEL_COMMANDS) / sizeof(VOCAT_PANEL_COMMANDS[0])> initialization{};
    for (size_t index = 0; index < initialization.size(); ++index) {
        const auto &command = VOCAT_PANEL_COMMANDS[index];
        initialization[index] = {command.code, command.data, command.length, command.delay_ms};
    }
    esp_lcd_panel_dev_config_t panel = {};
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    panel.reset_gpio_num = GPIO_NUM_1;
#else
    panel.reset_gpio_num = vocat_lcd_reset_pin();
    panel.flags.reset_active_high = vocat_lcd_reset_active_high();
#endif
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    // No Waveshare panel-ID probe/CS21 or TCA expander on this board.
    return st77916_qspi_panel_create(g_panel_io, &panel, initialization.data(), initialization.size(), &g_panel);
#elif defined(ASSISTANT_BOARD_TAIJI)
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_10; io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 0; io.pclk_hz = 40000000; io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32; io.lcd_param_bits = 8; io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), TAG, "Taiji QSPI IO");
    return taiji_panel_create(g_panel_io, &g_panel);
#else
    esp_lcd_panel_io_spi_config_t probe = {};
    probe.cs_gpio_num = GPIO_NUM_21;
    probe.dc_gpio_num = GPIO_NUM_NC;
    probe.spi_mode = 0;
    probe.pclk_hz = 3000000;
    probe.trans_queue_depth = 10;
    probe.lcd_cmd_bits = 32;
    probe.lcd_param_bits = 8;
    probe.flags.quad_mode = true;
    esp_lcd_panel_io_handle_t probe_io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &probe, &probe_io), TAG, "panel ID IO");
    std::array<uint8_t, 4> identifier{};
    const int read_id_command = (0x0B << 24) | (0x04 << 8);
    const esp_err_t read_result = esp_lcd_panel_io_rx_param(probe_io, read_id_command, identifier.data(), identifier.size());
    const esp_err_t cleanup = esp_lcd_panel_io_del(probe_io);
    if (cleanup != ESP_OK) ESP_LOGE(TAG, "release_panel_id_io failed: %s", esp_err_to_name(cleanup));
    // The low-speed device is gone before replacing it. A failed ID read must
    // never select an initialization sequence from uninitialized/default bytes.
    ESP_RETURN_ON_ERROR(read_result, TAG, "read ST77916 ID");
    ESP_RETURN_ON_ERROR(cleanup, TAG, "release panel ID IO");
    const bool version1 = identifier == std::array<uint8_t, 4>{0x00, 0x7F, 0x7F, 0x7F};
    const bool version2 = identifier == std::array<uint8_t, 4>{0x00, 0x02, 0x7F, 0x7F};
    if (!version1 && !version2) {
        ESP_LOGE(TAG, "unsupported_panel_id=%02x:%02x:%02x:%02x", identifier[0], identifier[1], identifier[2], identifier[3]);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "panel_id=%02x:%02x:%02x:%02x version=%d", identifier[0], identifier[1], identifier[2], identifier[3], version2 ? 2 : 1);
    probe.pclk_hz = 80000000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &probe, &g_panel_io), TAG, "panel display IO");
    const St77916Command *commands = ST77916_DEFAULT_PANEL;
    size_t command_count = sizeof(ST77916_DEFAULT_PANEL) / sizeof(ST77916_DEFAULT_PANEL[0]);
    if (version2) {
        commands = ST77916_PANEL_V2;
        command_count = sizeof(ST77916_PANEL_V2) / sizeof(ST77916_PANEL_V2[0]);
    }
#if defined(ASSISTANT_BOARD_LCD_185B)
    else {
        commands = ST77916_BOARD_B_V1;
        command_count = sizeof(ST77916_BOARD_B_V1) / sizeof(ST77916_BOARD_B_V1[0]);
    }
#endif
    // Original/C version1 retains the exact2.0.2 default table. B has its own
    // version1 sequence. The adapter and command bytes outlive the panel.
    static std::array<St77916QspiCommand, sizeof(ST77916_DEFAULT_PANEL) / sizeof(ST77916_DEFAULT_PANEL[0])> initialization{};
    if (command_count > initialization.size()) return ESP_ERR_INVALID_SIZE;
    for (size_t index = 0; index < command_count; ++index) {
        initialization[index] = {commands[index].code, commands[index].data, commands[index].length, commands[index].delay_ms};
    }
    esp_lcd_panel_dev_config_t panel = {};
#if defined(ASSISTANT_BOARD_LCD_185B)
    panel.reset_gpio_num = GPIO_NUM_3;
#else
    panel.reset_gpio_num = GPIO_NUM_NC;
#endif
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    return st77916_qspi_panel_create(g_panel_io, &panel, initialization.data(), command_count, &g_panel);
#endif
}
#endif

esp_err_t initialize_display() {
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "board I2C");
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    ESP_RETURN_ON_ERROR(moji2_c5_board_init(), TAG, "Moji2 PA/backlight setup");
#elif defined(ASSISTANT_BOARD_VOCAT)
    if (!vocat_board_ready()) return ESP_ERR_INVALID_STATE;
    // Runtime revision selects the direct reset pin/polarity at panel create.
#elif defined(ASSISTANT_BOARD_TAIJI)
    // Direct reset pins on Taiji;no TCA9554 or Waveshare ID-probe CS21.
#elif defined(ASSISTANT_BOARD_LCD_185B)
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << GPIO_NUM_3;
    reset.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "LCD reset direction");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_3, 0), TAG, "LCD reset assert");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_3, 1), TAG, "LCD reset release");
    vTaskDelay(pdMS_TO_TICKS(150));
#else
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(g_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &g_expander), TAG, "TCA9554");
    const uint32_t resets = IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1;
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(g_expander, resets, IO_EXPANDER_OUTPUT), TAG, "reset direction");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(g_expander, resets, 1), TAG, "reset initial");
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(g_expander, resets, 0), TAG, "reset assert");
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(g_expander, resets, 1), TAG, "reset release");
    vTaskDelay(pdMS_TO_TICKS(300));
#endif
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    backlight.gpio_num = GPIO_NUM_2;
#elif defined(ASSISTANT_BOARD_VOCAT)
    backlight.gpio_num = GPIO_NUM_44;
#elif defined(ASSISTANT_BOARD_TAIJI)
    backlight.gpio_num = GPIO_NUM_15;
#else
    backlight.gpio_num = GPIO_NUM_5;
#endif
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight channel");
    g_pwm_ready = true;
    spi_bus_config_t spi = {};
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    spi.data0_io_num = GPIO_NUM_9; spi.data1_io_num = GPIO_NUM_8;
    spi.data2_io_num = GPIO_NUM_7; spi.data3_io_num = GPIO_NUM_6;
    spi.sclk_io_num = GPIO_NUM_0;
#elif defined(ASSISTANT_BOARD_VOCAT)
    spi.data0_io_num = GPIO_NUM_46;
    spi.data1_io_num = GPIO_NUM_13;
    spi.data2_io_num = GPIO_NUM_11;
    spi.data3_io_num = GPIO_NUM_12;
    spi.sclk_io_num = GPIO_NUM_18;
#elif defined(ASSISTANT_BOARD_TAIJI)
    spi.data0_io_num = GPIO_NUM_11; spi.data1_io_num = GPIO_NUM_12;
    spi.data2_io_num = GPIO_NUM_13; spi.data3_io_num = GPIO_NUM_14;
    spi.sclk_io_num = GPIO_NUM_9;
#else
    spi.data0_io_num = GPIO_NUM_46; spi.data1_io_num = GPIO_NUM_45;
    spi.data2_io_num = GPIO_NUM_42; spi.data3_io_num = GPIO_NUM_41;
    spi.sclk_io_num = GPIO_NUM_40;
#endif
    spi.max_transfer_sz = kSize * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &spi, SPI_DMA_CH_AUTO), TAG, "QSPI bus");
#if defined(ASSISTANT_BOARD_LCD_146)
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_21;
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 3;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), TAG, "QSPI IO");
    ESP_RETURN_ON_ERROR(spd2010_qspi_panel_create(g_panel_io, &g_panel), TAG, "SPD2010 fixed QSPI");
#else
    ESP_RETURN_ON_ERROR(create_st77916_panel(), TAG, "ST77916 panel");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
#if !defined(ASSISTANT_BOARD_LCD_146)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, false), TAG, "panel axes");
#endif
#if defined(ASSISTANT_BOARD_TAIJI)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, true), TAG, "panel mirror");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "panel mirror");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "panel on");
#if BSP_CAPS_TOUCH
    esp_lcd_panel_io_i2c_config_t touch_io = {};
#if defined(ASSISTANT_BOARD_LCD_146)
    touch_io.dev_addr = 0x53;
    touch_io.lcd_cmd_bits = 0;
    touch_io.transaction_timeout_ms = 100;
#else
    touch_io.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    touch_io.lcd_cmd_bits = 8;
#endif
    touch_io.scl_speed_hz = 100000;
#if defined(ASSISTANT_BOARD_VOCAT)
    touch_io.transaction_timeout_ms = 100;
#endif
#if defined(ASSISTANT_BOARD_TAIJI)
    touch_io.transaction_timeout_ms = 1000;
#endif
    touch_io.control_phase_bytes = 1;
    touch_io.flags.disable_control_phase = true;
#if defined(ASSISTANT_BOARD_LCD_185)
    // The original1.85 routes touch to a second I2C bus on SDA1/SCL3.
    i2c_master_bus_config_t touch_bus = {};
    touch_bus.i2c_port = I2C_NUM_1;
    touch_bus.sda_io_num = GPIO_NUM_1;
    touch_bus.scl_io_num = GPIO_NUM_3;
    touch_bus.clk_source = I2C_CLK_SRC_DEFAULT;
    touch_bus.glitch_ignore_cnt = 7;
    touch_bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&touch_bus, &g_touch_bus), TAG, "touch I2C bus");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_touch_bus, &touch_io, &g_touch_io), TAG, "touch IO");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &touch_io, &g_touch_io), TAG, "touch IO");
#endif
#if defined(ASSISTANT_BOARD_LCD_146)
    gpio_config_t interrupt = {};
    interrupt.pin_bit_mask = 1ULL << GPIO_NUM_4;
    interrupt.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "SPD touch polling INT input");
    ESP_RETURN_ON_ERROR(g_touch.Init(g_touch_io), TAG, "SPD touch firmware read");
#else
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kSize; touch.y_max = kSize;
#if defined(ASSISTANT_BOARD_TAIJI)
    touch.rst_gpio_num = GPIO_NUM_40;
#elif defined(ASSISTANT_BOARD_LCD_185B)
    touch.rst_gpio_num = GPIO_NUM_1;
#else
    touch.rst_gpio_num = GPIO_NUM_NC;
#endif
    // Polling uses no IRQ handler; keep the board's actual IRQ pin as input.
#if defined(ASSISTANT_BOARD_VOCAT)
    touch.int_gpio_num = GPIO_NUM_10;
#elif defined(ASSISTANT_BOARD_TAIJI)
    touch.int_gpio_num = GPIO_NUM_41;
#else
    touch.int_gpio_num = GPIO_NUM_4;
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(g_touch_io, &touch, &g_touch), TAG, "CST816 touch");
#if defined(ASSISTANT_BOARD_VOCAT)
    ESP_RETURN_ON_ERROR(gpio_intr_disable(GPIO_NUM_10), TAG, "VoCat touch polling IRQ disabled");
#endif
#endif
#endif
    return ESP_OK;
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
#if defined(ASSISTANT_BOARD_MOJI2_C5)
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_26; bus.scl_io_num = GPIO_NUM_27;
#elif defined(ASSISTANT_BOARD_VOCAT)
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_2;
    bus.scl_io_num = GPIO_NUM_1;
#elif defined(ASSISTANT_BOARD_TAIJI)
    bus.i2c_port = I2C_NUM_1;
    bus.sda_io_num = GPIO_NUM_7;
    bus.scl_io_num = GPIO_NUM_8;
#else
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_11;
    bus.scl_io_num = GPIO_NUM_10;
#endif
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

lv_display_t *bsp_display_start() {
#if BSP_CAPS_TOUCH
    if (g_started) return g_pointer ? g_display : nullptr;
#else
    if (g_started) return g_display;
#endif
    g_started = true;
    if (initialize_display() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
#if CONFIG_IDF_TARGET_ESP32C5
    port.task_affinity = 0;
#else
    port.task_affinity = 1;
#endif
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io; display.panel_handle = g_panel;
    display.hres = kSize; display.vres = kSize;
    display.buffer_size = kSize * 20; display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true; display.flags.swap_bytes = true;
#if defined(ASSISTANT_BOARD_LCD_146)
    // The fixed SPD driver rejects swap_xy(false); software rotation keeps
    // rotation0 native and avoids the port's automatic hardware callback.
    display.flags.sw_rotate = true;
#endif
    display.rounder_cb = round_display_area;
#if defined(ASSISTANT_BOARD_TAIJI)
    display.rotation.mirror_x = true; display.rotation.mirror_y = true;
#endif
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
#if BSP_CAPS_TOUCH
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display);
            lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
#endif
    lvgl_port_unlock();
#if BSP_CAPS_TOUCH
    if (!g_display || !g_pointer) { ESP_LOGE(TAG, "LVGL display/touch allocation failed"); return nullptr; }
#else
    if (!g_display) { ESP_LOGE(TAG, "LVGL display allocation failed"); return nullptr; }
#endif
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), TAG, "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
