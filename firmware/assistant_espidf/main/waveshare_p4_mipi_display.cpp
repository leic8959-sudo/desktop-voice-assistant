#include "waveshare_p4_mipi_board.h"

#include "driver/ledc.h"
#include "esp_check.h"
#if defined(ASSISTANT_BOARD_P4_ST7701)
#include "waveshare_p4_st7701_cmds.h"
#elif defined(ASSISTANT_BOARD_P4_ILI9881C)
#include "waveshare_p4_ili9881c_cmds.h"
#elif defined(ASSISTANT_BOARD_P4_7B)
#include "esp_lcd_ek79007.h"
#elif defined(ASSISTANT_BOARD_P4_ST7703)
#include "esp_lcd_st7703.h"
#elif defined(ASSISTANT_BOARD_P4_NANO)
#include "waveshare_p4_nano_cmds.h"
#elif defined(ASSISTANT_BOARD_P4_JD9365)
#include "waveshare_p4_jd9365_cmds.h"
#else
#include "esp_lcd_hx8394.h"
#endif
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#if defined(ASSISTANT_BOARD_P4_NANO)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {
constexpr const char *TAG = "waveshare_p4_mipi";
using waveshare_p4_mipi::panel;
constexpr int kWidth = panel.width, kHeight = panel.height;
#if !defined(ASSISTANT_BOARD_P4_NANO)
constexpr gpio_num_t kBacklight = panel.backlight;
#endif
bool g_pads_ready;
i2c_master_bus_handle_t g_i2c;
esp_lcd_dsi_bus_handle_t g_dsi;
esp_ldo_channel_handle_t g_phy_power;
esp_lcd_panel_io_handle_t g_panel_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_attempted, g_ready, g_backlight_ready, g_touch_failed;
bool g_wait_for_release = true;
#if defined(ASSISTANT_BOARD_P4_NANO)
i2c_master_dev_handle_t g_backlight_control;

esp_err_t ensure_i2c() {
    if (g_i2c) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = GPIO_NUM_7; bus.scl_io_num = GPIO_NUM_8;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_i2c);
}

esp_err_t write_backlight_register(uint8_t reg, uint8_t value) {
    if (!g_backlight_control) return ESP_ERR_INVALID_STATE;
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(g_backlight_control, bytes, sizeof(bytes), 50);
}

esp_err_t prepare_backlight_control() {
    ESP_RETURN_ON_ERROR(ensure_i2c(), TAG, "Nano I2C1");
    if (!g_backlight_control) {
        i2c_device_config_t device = {};
        device.device_address = 0x45; device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.scl_speed_hz = 100000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &device, &g_backlight_control), TAG, "Nano backlight control");
    }
    // Keep the owning power sequence, then stay dark until the UI is ready.
    // This checks write ACKs; register readback is not established by the source.
    const uint8_t commands[][2] = {{0x95, 0x11}, {0x95, 0x17}, {0x96, 0x00}};
    for (const auto &command : commands) {
        const esp_err_t result = write_backlight_register(command[0], command[1]);
        if (result != ESP_OK) {
            const esp_err_t dark = write_backlight_register(0x96, 0);
            ESP_LOGE(TAG, "Nano control reg=0x%02x value=0x%02x failed:%s dark=%s", command[0], command[1], esp_err_to_name(result), esp_err_to_name(dark));
            return result;
        }
    }
    // The matched manufacturer driver allows one second for panel power.
    // Preserve that settling interval while keeping the backlight at zero.
    vTaskDelay(pdMS_TO_TICKS(1000));
    g_backlight_ready = true;
    return ESP_OK;
}
#endif

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    esp_lcd_touch_point_data_t points[2]{};
    uint8_t count = 0;
    esp_err_t result = esp_lcd_touch_read_data(g_touch);
    if (result == ESP_OK) result = esp_lcd_touch_get_data(g_touch, points, &count, 2);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "GT911 read failed:%s", esp_err_to_name(result));
        g_touch_failed = true; g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "GT911 recovered;waiting for release");
    g_touch_failed = false;
    if (!count) { g_wait_for_release = false; return; }
    if (count != 1 || points[0].x >= kWidth || points[0].y >= kHeight) {
        g_wait_for_release = true;
        lv_indev_reset(pointer, nullptr);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = points[0].x; data->point.y = points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t darken() {
#if defined(ASSISTANT_BOARD_P4_NANO)
    return write_backlight_register(0x96, 0);
#else
    if (!g_backlight_ready) return gpio_set_level(kBacklight, waveshare_p4_mipi::backlight_off_level);
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, waveshare_p4_mipi::backlight_duty(0)), TAG, "backlight dark duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

esp_err_t initialize_display() {
    ESP_RETURN_ON_ERROR(waveshare_p4_mipi_prepare(), TAG, "display pads");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "codec/touch I2C1");
#if !defined(ASSISTANT_BOARD_P4_NANO)
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_10_BIT; timer.freq_hz = 5000; timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t channel = {};
    channel.gpio_num = kBacklight; channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0; channel.timer_sel = LEDC_TIMER_0; channel.duty = waveshare_p4_mipi::backlight_duty(0);
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight PWM");
    g_backlight_ready = true;
#endif
    esp_ldo_channel_config_t power = {};
    power.chan_id = 3; power.voltage_mv = 2500;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &g_phy_power), TAG, "DSI PHY LDO3");
    esp_lcd_dsi_bus_config_t bus = {};
    bus.bus_id = 0; bus.num_data_lanes = 2; bus.lane_bit_rate_mbps = panel.lane_rate_mbps;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &g_dsi), TAG, "DSI bus");
    esp_lcd_dbi_io_config_t io = {};
    io.virtual_channel = 0; io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(g_dsi, &io, &g_panel_io), TAG, "DBI panel IO");
    // Fixed panel-specific timing, shared allocation and error handling.
    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT; dpi.dpi_clock_freq_mhz = panel.dpi_clock_mhz;
    dpi.in_color_format = LCD_COLOR_FMT_RGB565; dpi.out_color_format = LCD_COLOR_FMT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = kWidth; dpi.video_timing.v_size = kHeight;
    dpi.video_timing.hsync_pulse_width = panel.hsync; dpi.video_timing.hsync_back_porch = panel.hback; dpi.video_timing.hsync_front_porch = panel.hfront;
    dpi.video_timing.vsync_pulse_width = panel.vsync; dpi.video_timing.vsync_back_porch = panel.vback; dpi.video_timing.vsync_front_porch = panel.vfront;
    dpi.flags.use_dma2d = true;
    esp_lcd_panel_dev_config_t config = {};
    config.reset_gpio_num = panel.reset;
    config.flags.reset_active_high = panel.reset_active_high;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; config.bits_per_pixel = 16;
#if defined(ASSISTANT_BOARD_P4_ST7701)
    st7701_vendor_config_t vendor = {};
    vendor.init_cmds = waveshare_p4_st7701_init_cmds;
    vendor.init_cmds_size = sizeof(waveshare_p4_st7701_init_cmds) / sizeof(waveshare_p4_st7701_init_cmds[0]);
    vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi;
    vendor.flags.use_mipi_interface = true;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(g_panel_io, &config, &g_panel), TAG, "ST7701 create");
#elif defined(ASSISTANT_BOARD_P4_ILI9881C)
    ili9881c_vendor_config_t vendor = {};
    vendor.init_cmds = waveshare_p4_ili9881c_init_cmds;
    vendor.init_cmds_size = sizeof(waveshare_p4_ili9881c_init_cmds) / sizeof(waveshare_p4_ili9881c_init_cmds[0]);
    vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi; vendor.mipi_config.lane_num = 2;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9881c(g_panel_io, &config, &g_panel), TAG, "ILI9881C create");
#elif defined(ASSISTANT_BOARD_P4_7B)
    ek79007_vendor_config_t vendor = {};
    vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi; vendor.mipi_config.lane_num = 2;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ek79007(g_panel_io, &config, &g_panel), TAG, "EK79007 create");
#elif defined(ASSISTANT_BOARD_P4_ST7703)
    st7703_vendor_config_t vendor = {};
    vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi;
    vendor.flags.use_mipi_interface = true;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7703(g_panel_io, &config, &g_panel), TAG, "ST7703 create");
#elif defined(ASSISTANT_BOARD_P4_JD9365) || defined(ASSISTANT_BOARD_P4_NANO)
    jd9365_vendor_config_t vendor = {};
#if defined(ASSISTANT_BOARD_P4_NANO)
    vendor.init_cmds = waveshare_p4_nano_init_cmds;
    vendor.init_cmds_size = sizeof(waveshare_p4_nano_init_cmds) / sizeof(waveshare_p4_nano_init_cmds[0]);
#else
    vendor.init_cmds = waveshare_p4_jd9365_init_cmds;
    vendor.init_cmds_size = sizeof(waveshare_p4_jd9365_init_cmds) / sizeof(waveshare_p4_jd9365_init_cmds[0]);
#endif
    vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi; vendor.mipi_config.lane_num = 2;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(g_panel_io, &config, &g_panel), TAG, "JD9365 create");
#else
    // The matched manufacturer BSP also skips the standalone driver's extra
    // I2C1 creation and0x45 writes. This board already owns I2C1 and LDO3.
    hx8394_vendor_config_t vendor = {};
    vendor.mipi_config.dsi_bus = g_dsi; vendor.mipi_config.dpi_config = &dpi; vendor.mipi_config.lane_num = 2;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_hx8394(g_panel_io, &config, &g_panel), TAG, "HX8394 create");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
#if !defined(ASSISTANT_BOARD_P4_HX8394)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "panel orientation");
#endif
    // HX8394's matched driver has no mirror operation. Its default command
    // table owns the fixed orientation. Screen-off always uses the backlight.

    int address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    esp_err_t probe = i2c_master_probe(g_i2c, address, 100);
    if (probe == ESP_ERR_NOT_FOUND) {
        address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        probe = i2c_master_probe(g_i2c, address, 100);
    }
    ESP_RETURN_ON_ERROR(probe, TAG, "GT911 known addresses");
    esp_lcd_panel_io_i2c_config_t touch_io = {};
    touch_io.dev_addr = address;
#if defined(ASSISTANT_BOARD_P4_NANO)
    touch_io.scl_speed_hz = 100000;
#else
    touch_io.scl_speed_hz = 400000;
#endif
    touch_io.control_phase_bytes = 1; touch_io.lcd_cmd_bits = 16;
    touch_io.flags.disable_control_phase = true; touch_io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_i2c, &touch_io, &g_touch_io), TAG, "GT911 IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = kWidth; touch.y_max = kHeight;
    touch.rst_gpio_num = GPIO_NUM_NC; touch.int_gpio_num = GPIO_NUM_NC;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(g_touch_io, &touch, &g_touch), TAG, "GT911 init");

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1; port.task_priority = 2;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "LVGL port");
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io; display.panel_handle = g_panel;
    display.hres = kWidth; display.vres = kHeight; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.buffer_size = kWidth * waveshare_p4_mipi::draw_rows; display.double_buffer = true;
    display.flags.buff_spiram = true;
    // Avoid LVGL port attempting the DPI driver's unsupported swap_xy API.
    // This allocates one additional20line rotation buffer even at rotation0.
    display.flags.sw_rotate = true;
    lvgl_port_display_dsi_cfg_t dsi = {};
    g_display = lvgl_port_add_disp_dsi(&display, &dsi);
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display); lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    g_ready = g_display && g_pointer;
    lvgl_port_unlock();
    if (!g_ready) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "display ready panel=%s %dx%d DSI=%dMbps DPI=%dMHz framebuffer=%u drawbuffers=%u GT911=0x%02x;backlight dark", panel.name, kWidth, kHeight, panel.lane_rate_mbps, panel.dpi_clock_mhz, static_cast<unsigned>(kWidth * kHeight * 2), static_cast<unsigned>(3 * kWidth * waveshare_p4_mipi::draw_rows * 2), address);
    return ESP_OK;
}
}

esp_err_t waveshare_p4_mipi_prepare() {
    if (g_pads_ready) return ESP_OK;
#if !defined(ASSISTANT_BOARD_P4_NANO)
    ESP_RETURN_ON_ERROR(gpio_set_level(kBacklight, waveshare_p4_mipi::backlight_off_level), TAG, "backlight off latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << kBacklight; backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight pad");
#endif
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_53, 0), TAG, "PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_53; amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), TAG, "PA53 off");
#if defined(ASSISTANT_BOARD_P4_NANO)
    ESP_RETURN_ON_ERROR(prepare_backlight_control(), TAG, "Nano power/backlight dark");
#endif
    g_pads_ready = true;
    return ESP_OK;
}

esp_err_t bsp_i2c_init() {
#if defined(ASSISTANT_BOARD_P4_NANO)
    // prepare creates I2C without recursing through this public wrapper.
    return waveshare_p4_mipi_prepare();
#else
    if (g_i2c) return ESP_OK;
    ESP_RETURN_ON_ERROR(waveshare_p4_mipi_prepare(), TAG, "board pads");
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = GPIO_NUM_7; bus.scl_io_num = GPIO_NUM_8;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_i2c);
#endif
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }

lv_display_t *bsp_display_start() {
    if (g_attempted) return g_ready ? g_display : nullptr;
    g_attempted = true;
    const esp_err_t result = initialize_display();
    if (result != ESP_OK) {
        g_ready = false;
        const esp_err_t dark = darken(), mute = gpio_set_level(GPIO_NUM_53, 0);
        ESP_LOGE(TAG, "display stopped:%s dark=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
    }
    // Keep partial objects alive: a panel or LVGL callback may retain handles.
    return g_ready ? g_display : nullptr;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready || !g_backlight_ready) return ESP_ERR_INVALID_STATE;
#if defined(ASSISTANT_BOARD_P4_NANO)
    ESP_RETURN_ON_ERROR(write_backlight_register(0x96, static_cast<uint8_t>(percent * 255 / 100)), TAG, "Nano brightness write ACK");
    return ESP_OK;
#else
    const uint32_t duty = waveshare_p4_mipi::backlight_duty(percent);
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), TAG, "brightness duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}
