#include "board_profile.h"

#include <array>
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if !defined(ASSISTANT_BOARD_WS349)
#include "waveshare_35_commands.h"
#endif
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
#include "axs_qspi_panel.h"
#else
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_ft5x06.h"
#endif

namespace {
constexpr const char *TAG = "waveshare_lcd_display";
constexpr int kWidth = board_profile::width, kHeight = board_profile::height;
#if defined(ASSISTANT_BOARD_WS349)
constexpr int kPhysicalWidth = kWidth, kPhysicalHeight = kHeight, kTransferRows = 64;
#if defined(ASSISTANT_BOARD_WS349_V2)
constexpr gpio_num_t kBacklight = GPIO_NUM_42;
#else
constexpr gpio_num_t kBacklight = GPIO_NUM_8;
#endif
#elif defined(ASSISTANT_BOARD_WS35B)
constexpr int kPhysicalWidth = kHeight, kPhysicalHeight = kWidth, kTransferRows = 20;
constexpr gpio_num_t kBacklight = GPIO_NUM_6;
#else
constexpr gpio_num_t kBacklight = GPIO_NUM_6;
#endif
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
static_assert(kPhysicalHeight % kTransferRows == 0, "AXS frame must contain complete stripes");
#endif
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_io_handle_t g_panel_io;
esp_lcd_panel_handle_t g_panel;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_started, g_ready, g_pwm_ready, g_wait_for_release = true;
esp_err_t g_touch_error = ESP_OK;
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
i2c_master_dev_handle_t g_touch_device;
#if defined(ASSISTANT_BOARD_WS349)
i2c_master_bus_handle_t g_touch_bus;
#endif
SemaphoreHandle_t g_transfer_done;
uint16_t *g_transfer;
bool g_flush_failed;
#else
esp_lcd_panel_io_handle_t g_touch_io;
esp_lcd_touch_handle_t g_touch;
#endif

#if !defined(ASSISTANT_BOARD_WS35B) && !defined(ASSISTANT_BOARD_WS349)
esp_err_t reset_st7796_panel(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x01, nullptr, 0), TAG, "ST7796 software reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t initialize_st7796_panel(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "ST7796 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x05;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &axes, 1), TAG, "ST7796 initial axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "ST7796 initial format");
    for (const auto &command : WAVESHARE_35_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "ST7796 command 0x%02x", command.code);
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}
#endif

void cancel_touch(lv_indev_t *pointer) {
    g_wait_for_release = true;
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    uint8_t count = 0, event = 0;
    uint16_t x = 0, y = 0;
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
    // Public AXS15231B2.1.0 protocol. Read the raw count/event because its
    // one-point driver drops count>1 instead of reporting cancellation.
#if defined(ASSISTANT_BOARD_WS349)
    // Owning3.49 uses a repeated-start request for14bytes but reads32.
    // Read exactly the requested packet; raw count still detects extra points.
    const uint8_t command[] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 14, 0, 0, 0};
    uint8_t packet[14]{};
    const esp_err_t result = i2c_master_transmit_receive(g_touch_device, command, sizeof(command), packet, sizeof(packet), 100);
#else
    const uint8_t command[] = {0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 8, 0, 0, 0};
    uint8_t packet[8]{};
    esp_err_t result = i2c_master_transmit(g_touch_device, command, sizeof(command), 100);
    if (result == ESP_OK) result = i2c_master_receive(g_touch_device, packet, sizeof(packet), 100);
#endif
    count = packet[1];
    event = packet[2] >> 6;
    x = static_cast<uint16_t>((packet[2] & 0x0f) << 8) | packet[3];
    y = static_cast<uint16_t>((packet[4] & 0x0f) << 8) | packet[5];
#else
    // FT5x06 status and first record are contiguous. Inspect its event bits,
    // which the generic1.1.1 coordinate getter does not expose.
    uint8_t packet[7]{};
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    count = packet[0] & 0x0f;
    event = packet[1] >> 6;
    x = static_cast<uint16_t>((packet[1] & 0x0f) << 8) | packet[2];
    y = static_cast<uint16_t>((packet[3] & 0x0f) << 8) | packet[4];
#endif
    if (result != ESP_OK) {
        if (result != g_touch_error) ESP_LOGE(TAG, "touch_read_failed error=%s", esp_err_to_name(result));
        g_touch_error = result;
        cancel_touch(pointer);
        return;
    }
    if (g_touch_error != ESP_OK) ESP_LOGI(TAG, "touch recovered; wait for release");
    g_touch_error = ESP_OK;
    if (!count || (count == 1 && event == 1)) { g_wait_for_release = false; return; }
    if (count != 1 || (event != 0 && event != 2) || x >= kHeight || y >= kWidth) {
        cancel_touch(pointer);
        return;
    }
    if (g_wait_for_release) return;
#if defined(ASSISTANT_BOARD_WS349)
    // Correct the source's pre-swap clamps: rawX spans640,rawY spans172.
    // Preserve its swap/mirror intent without clamping bad points into clicks.
    data->point.x = y;
    data->point.y = kHeight - 1 - x;
#else
    // Retain the owning boards' mirrorX/mirrorY then swapXY intent, using
    // the physical320x480 extent and zero-based endpoints. Source config
    // used landscape maxima before swapping, producing out-of-range Y.
    data->point.x = kWidth - 1 - y;
    data->point.y = kHeight - 1 - x;
#endif
    data->state = LV_INDEV_STATE_PRESSED;
}

#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
bool transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *) {
    BaseType_t awakened = pdFALSE;
    xSemaphoreGiveFromISR(g_transfer_done, &awakened);
    return awakened == pdTRUE;
}

void flush_full_frame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    if (g_flush_failed) { lv_display_flush_ready(display); return; }
    esp_err_t result = ESP_OK;
    if (area->x1 != 0 || area->y1 != 0 || area->x2 != kWidth - 1 || area->y2 != kHeight - 1) {
        result = ESP_ERR_INVALID_SIZE;
    }
    const int64_t deadline = esp_timer_get_time() + 1000000;
    const auto *source = reinterpret_cast<const uint16_t *>(pixels);
    // QSPI RAMWR starts row0; RAMWRC continues sequentially. Every flush
    // sends a complete native frame, even after a small object invalidation.
    for (int row = 0; result == ESP_OK && row < kPhysicalHeight; row += kTransferRows) {
        for (int line = 0; line < kTransferRows; ++line) {
            for (int column = 0; column < kPhysicalWidth; ++column) {
#if defined(ASSISTANT_BOARD_WS349)
                const uint16_t color = source[(row + line) * kWidth + column];
#else
                // Same90-degree mapping as the fixed3.5B custom flush.
                const uint16_t color = source[(kHeight - 1 - column) * kWidth + row + line];
#endif
                g_transfer[line * kPhysicalWidth + column] = static_cast<uint16_t>((color >> 8) | (color << 8));
            }
        }
        const uint8_t columns[] = {0, 0, static_cast<uint8_t>((kPhysicalWidth - 1) >> 8), static_cast<uint8_t>(kPhysicalWidth - 1)};
        // The component's draw_bitmap discards CASET errors. Keep its QSPI
        // framing but check the public IO result before queuing color data.
        result = esp_lcd_panel_io_tx_param(g_panel_io, 0x02002a00, columns, sizeof(columns));
        if (result != ESP_OK) break;
        xSemaphoreTake(g_transfer_done, 0);
        result = esp_lcd_panel_io_tx_color(g_panel_io, row == 0 ? 0x32002c00 : 0x32003c00,
            g_transfer, kPhysicalWidth * kTransferRows * sizeof(uint16_t));
        if (result != ESP_OK) break;
        const int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0 || xSemaphoreTake(g_transfer_done,
            pdMS_TO_TICKS(static_cast<uint32_t>((remaining + 999) / 1000)) + 1) != pdTRUE) result = ESP_ERR_TIMEOUT;
        // Completion is awaited even for the last stripe. Never overwrite
        // DMA memory or release this LVGL frame while a stripe is in flight.
    }
    if (result != ESP_OK) {
        g_flush_failed = true;
        const esp_err_t dark = bsp_display_brightness_set(0);
        ESP_LOGE(TAG, "display_transfer_stopped error=%s dark=%s; restart to retry", esp_err_to_name(result), esp_err_to_name(dark));
        // A timed-out DMA may still own g_transfer. Retain it and never
        // write it again; it is independent of the released LVGL framebuffer.
    }
    lv_display_flush_ready(display);
}
#endif

esp_err_t initialize_touch() {
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
#if defined(ASSISTANT_BOARD_WS349)
    i2c_master_bus_config_t touch_bus = {};
    touch_bus.i2c_port = I2C_NUM_1;
    touch_bus.sda_io_num = GPIO_NUM_17;
    touch_bus.scl_io_num = GPIO_NUM_18;
    touch_bus.clk_source = I2C_CLK_SRC_DEFAULT;
    touch_bus.glitch_ignore_cnt = 7;
    touch_bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&touch_bus, &g_touch_bus), TAG, "AXS touch I2C1");
    const auto bus = g_touch_bus;
#else
    const auto bus = g_bus;
#endif
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x3b, 100), TAG, "AXS touch presence");
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x3b;
#if defined(ASSISTANT_BOARD_WS349)
    device.scl_speed_hz = 300000;
#else
    device.scl_speed_hz = 400000;
#endif
    return i2c_master_bus_add_device(bus, &device, &g_touch_device);
#else
    // Keep FT5x06 defaults without its SDK-version-dependent C++ field order.
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    io.control_phase_bytes = 1; io.dc_bit_offset = 0; io.lcd_cmd_bits = 8;
    io.flags.disable_control_phase = 1;
    io.scl_speed_hz = 400000;
    io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), TAG, "FT5x06 IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kHeight;
    touch.y_max = kWidth;
    touch.rst_gpio_num = GPIO_NUM_NC;
    touch.int_gpio_num = GPIO_NUM_NC;
    // Use the official init/presence path. Coordinate transforms and event
    // validation are handled once in our pointer reader, never twice.
    return esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch);
#endif
}

esp_err_t initialize_display() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = kBacklight;
#if defined(ASSISTANT_BOARD_WS349)
    backlight.duty = 1023;
#endif
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight initially dark");
    g_pwm_ready = true;
    spi_bus_config_t bus = {};
#if defined(ASSISTANT_BOARD_WS349)
    bus.sclk_io_num = GPIO_NUM_10;
    bus.data0_io_num = GPIO_NUM_11;
    bus.data1_io_num = GPIO_NUM_12;
    bus.data2_io_num = GPIO_NUM_13;
    bus.data3_io_num = GPIO_NUM_14;
    bus.max_transfer_sz = kPhysicalWidth * kTransferRows * sizeof(uint16_t);
    constexpr spi_host_device_t host = SPI3_HOST;
#elif defined(ASSISTANT_BOARD_WS35B)
    bus.sclk_io_num = GPIO_NUM_5;
    bus.data0_io_num = GPIO_NUM_1;
    bus.data1_io_num = GPIO_NUM_2;
    bus.data2_io_num = GPIO_NUM_3;
    bus.data3_io_num = GPIO_NUM_4;
    bus.max_transfer_sz = kHeight * 20 * sizeof(uint16_t);
    constexpr spi_host_device_t host = SPI2_HOST;
#else
    bus.sclk_io_num = GPIO_NUM_5;
    bus.mosi_io_num = GPIO_NUM_1;
    bus.miso_io_num = GPIO_NUM_2;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = kWidth * 20 * sizeof(uint16_t);
    constexpr spi_host_device_t host = SPI3_HOST;
#endif
    ESP_RETURN_ON_ERROR(spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO), TAG, "display bus");
    esp_lcd_panel_io_spi_config_t io = {};
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 10;
    io.lcd_param_bits = 8;
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
#if defined(ASSISTANT_BOARD_WS349)
    io.cs_gpio_num = GPIO_NUM_9;
#else
    io.cs_gpio_num = GPIO_NUM_12;
#endif
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 3;
    io.lcd_cmd_bits = 32;
    io.flags.quad_mode = true;
#else
    io.cs_gpio_num = GPIO_NUM_NC;
    io.dc_gpio_num = GPIO_NUM_3;
    io.spi_mode = 0;
    io.lcd_cmd_bits = 8;
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(host, &io, &g_panel_io), TAG, "display IO");
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
#if defined(ASSISTANT_BOARD_WS349)
    // Both fixed3.49 board tables contain only these two commands; native
    // hardware reset was completed by its board helper, not by a GPIO=-1.
    static const AxsQspiCommand commands[] = {{0x11, nullptr, 0, 100}, {0x29, nullptr, 0, 100}};
    constexpr size_t command_count = sizeof(commands) / sizeof(commands[0]);
    const AxsQspiCommand *initialization = commands;
#else
    static std::array<AxsQspiCommand, sizeof(WAVESHARE_35_COMMANDS) / sizeof(WAVESHARE_35_COMMANDS[0])> commands{};
    for (size_t index = 0; index < commands.size(); ++index) {
        const auto &command = WAVESHARE_35_COMMANDS[index];
        commands[index] = {command.code, command.data, command.length, command.delay_ms};
    }
    constexpr size_t command_count = commands.size();
    const AxsQspiCommand *initialization = commands.data();
#endif
    ESP_RETURN_ON_ERROR(axs_qspi_panel_create(g_panel_io, kPhysicalWidth, kPhysicalHeight,
        initialization, command_count, &g_panel), TAG, "AXS15231B fixed QSPI panel");
    // AXS reset/invert/mirror callbacks ignore IO failures in2.1.0. Use
    // checked public IO for these commands and keep native panel axes.
#if !defined(ASSISTANT_BOARD_WS349)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x02000100, nullptr, 0), TAG, "AXS SWRESET");
    vTaskDelay(pdMS_TO_TICKS(120));
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "AXS vendor initialization");
    const uint8_t orientation = 0;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x02003600, &orientation, 1), TAG, "AXS native axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x02002000, nullptr, 0), TAG, "AXS inversion off");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x02002900, nullptr, 0), TAG, "AXS display on");
#else
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_NC;
    panel.bits_per_pixel = 16;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    static_assert([] {
        for (const auto &command : WAVESHARE_35_COMMANDS) {
            if (command.length > sizeof(command.data) || command.code == 0x36) return false;
            if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x05)) return false;
        }
        return true;
    }(), "Waveshare3.5 table must preserve the reviewed BGR/RGB565 state");
    // Explicitly send the original table: the SDK default init ignores it and
    // writes a different format. Only its DCS drawing/rotation ops are reused.
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "ST7796 DCS operations");
    g_panel->reset = reset_st7796_panel;
    g_panel->init = initialize_st7796_panel;
    g_panel->disp_sleep = nullptr;
    g_panel->set_brightness = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "ST7796 software reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "ST7796 vendor initialization");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), TAG, "ST7796 invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "ST7796 landscape");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "ST7796 mirrors");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "ST7796 display on");
#endif
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
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
    g_transfer_done = xSemaphoreCreateBinary();
    g_transfer = static_cast<uint16_t *>(heap_caps_aligned_alloc(64,
        kPhysicalWidth * kTransferRows * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!g_transfer_done || !g_transfer) return nullptr;
#endif
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK || !lvgl_port_lock(1000)) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io;
    display.panel_handle = g_panel;
    display.hres = kWidth;
    display.vres = kHeight;
    display.color_format = LV_COLOR_FORMAT_RGB565;
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
    display.buffer_size = kWidth * kHeight;
    display.flags.buff_spiram = true;
    display.flags.full_refresh = true;
    // Shared complete-frame flush owns each panel's mapping and byte order.
#else
    display.buffer_size = kWidth * 20;
    display.double_buffer = true;
    display.flags.buff_dma = true;
    display.flags.swap_bytes = true;
    display.rotation.swap_xy = true;
#endif
    g_display = lvgl_port_add_disp(&display);
    esp_err_t callback_result = ESP_OK;
#if defined(ASSISTANT_BOARD_WS35B) || defined(ASSISTANT_BOARD_WS349)
    if (g_display) {
        lv_display_set_flush_cb(g_display, flush_full_frame);
        esp_lcd_panel_io_callbacks_t callbacks = {};
        callbacks.on_color_trans_done = transfer_done;
        callback_result = esp_lcd_panel_io_register_event_callbacks(g_panel_io, &callbacks, nullptr);
    }
#endif
    if (g_display && callback_result == ESP_OK) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display);
            lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    g_ready = g_display && g_pointer && callback_result == ESP_OK;
    lvgl_port_unlock();
    if (!g_ready) ESP_LOGE(TAG, "display/touch registration failed callback=%s", esp_err_to_name(callback_result));
    return g_ready ? g_display : nullptr;
}

bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
#if defined(ASSISTANT_BOARD_WS349)
    ESP_RETURN_ON_ERROR(waveshare_349_set_backlight_enable(false), TAG, "backlight gate off");
    const int duty = (100 - percent) * 1023 / 100;
#else
    const int duty = percent * 1023 / 100;
#endif
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), TAG, "backlight duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0), TAG, "backlight update");
#if defined(ASSISTANT_BOARD_WS349)
    return waveshare_349_set_backlight_enable(percent > 0);
#else
    return ESP_OK;
#endif
}
