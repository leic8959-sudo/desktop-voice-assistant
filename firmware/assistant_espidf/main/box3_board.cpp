#include "box3_board.h"
#include "box3_panel_commands.h"
#if defined(ASSISTANT_BOARD_BOX3)
#include "gt911_polling.h"
#endif
#if BSP_CAPS_TOUCH
#include "tt21100_polling.h"
#endif

#include "driver/ledc.h"
#if BSP_CAPS_SDCARD
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#endif
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *kTag = "box3_board";
i2c_master_bus_handle_t g_bus;
esp_lcd_panel_io_handle_t g_io;
esp_lcd_panel_handle_t g_panel;
#if defined(ASSISTANT_BOARD_BOX3)
esp_lcd_panel_io_handle_t g_touch_io;
Gt911Polling g_gt911;
#endif
#if BSP_CAPS_TOUCH
Tt21100Polling g_tt21100;
lv_indev_t *g_pointer;
bool g_touch_is_tt, g_wait_release = true, g_touch_failed;
#endif
lv_display_t *g_display;
bool g_bus_attempted, g_display_attempted, g_ready, g_pwm_ready;

#if BSP_CAPS_TOUCH
esp_err_t probe(uint8_t address, bool *found) {
    const esp_err_t result = i2c_master_probe(g_bus, address, 100);
    *found = result == ESP_OK;
    return result == ESP_ERR_NOT_FOUND ? ESP_OK : result;
}
#endif

esp_err_t reset_panel(esp_lcd_panel_t *) {
    // BOX3 is active high; original BOX and Lite are active low.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, box_board::reset_active_high ? 1 : 0), kTag, "shared reset active");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, box_board::reset_active_high ? 0 : 1), kTag, "shared reset inactive");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t initialize_ili9341(esp_lcd_panel_t *) {
    // Keep the reviewed ILI9341 preamble and eleven actual parameter rows.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x11, nullptr, 0), kTag, "ILI9341 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x36, &axes, 1), kTag, "BGR axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x3a, &format, 1), kTag, "RGB565");
    for (const auto &command : BOX3_PANEL_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, command.code, command.data, command.length), kTag, "ILI9341 command%02x", command.code);
    }
    // The fixed BSP retains old0x80/0xff length markers. Its modern driver
    // treats them as literal lengths, exceeding their one-byte payloads.
    // Send parameterless DCS commands with an explicit sleep-out settling
    // interval; the end marker is not a panel command.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x11, nullptr, 0), kTag, "table sleep out");
    vTaskDelay(pdMS_TO_TICKS(120));
    return esp_lcd_panel_io_tx_param(g_io, 0x29, nullptr, 0);
}

esp_err_t sleep_ili9341(esp_lcd_panel_t *, bool sleeping) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, sleeping ? 0x10 : 0x11, nullptr, 0), kTag, "ILI9341 sleep");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t create_panel() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_6; bus.miso_io_num = GPIO_NUM_NC; bus.sclk_io_num = GPIO_NUM_7;
    bus.quadwp_io_num = GPIO_NUM_NC; bus.quadhd_io_num = GPIO_NUM_NC; bus.max_transfer_sz = 320 * 20 * 2;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "SPI3");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_5; io.dc_gpio_num = GPIO_NUM_4; io.spi_mode = 0;
    io.pclk_hz = 40000000; io.lcd_cmd_bits = 8; io.lcd_param_bits = 8; io.trans_queue_depth = 10;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &g_io), kTag, "display IO");
#if defined(ASSISTANT_BOARD_BOX3)
    bool tt_found = false;
    ESP_RETURN_ON_ERROR(probe(0x24, &tt_found), kTag, "display revision probe");
#endif
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, box_board::reset_active_high ? 0 : 1), kTag, "shared reset idle latch");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_48; panel.flags.reset_active_high = box_board::reset_active_high;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR; panel.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_io, &panel, &g_panel), kTag, "panel DCS operations");
    g_panel->reset = reset_panel;
#if defined(ASSISTANT_BOARD_BOX3)
    if (!tt_found) { g_panel->init = initialize_ili9341; g_panel->disp_sleep = sleep_ili9341; }
#else
    // Pinned XiaoZhi BOX/Lite both supply this same ILI9341 vendor table.
    // BGR cache08 agrees with the table's MADCTL08 even though those board
    // classes initially pass RGB to their external factory.
    g_panel->init = initialize_ili9341; g_panel->disp_sleep = sleep_ili9341;
#endif
    g_panel->set_brightness = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), kTag, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), kTag, "panel init");
#if defined(ASSISTANT_BOARD_BOXLITE)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), kTag, "Lite invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), kTag, "Lite mirror");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, true), kTag, "panel mirror");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), kTag, "display on");
#if defined(ASSISTANT_BOARD_BOX3)
    ESP_LOGI(kTag, "display branch=%s based on TT21100 probe;physical identity unconfirmed", tt_found ? "ST7789" : "ILI9341");
#endif
    return ESP_OK;
}

#if BSP_CAPS_TOUCH
esp_err_t create_touch() {
    bool found = false;
    uint8_t address = 0;
#if defined(ASSISTANT_BOARD_BOX3)
    constexpr uint8_t addresses[] = {0x5d, 0x14, 0x24};
#else
    constexpr uint8_t addresses[] = {0x24};
#endif
    for (const uint8_t candidate : addresses) {
        ESP_RETURN_ON_ERROR(probe(candidate, &found), kTag, "touch probe");
        if (found) { address = candidate; break; }
    }
    if (!address) return ESP_ERR_NOT_FOUND;
    gpio_config_t interrupt = {};
    interrupt.pin_bit_mask = 1ULL << GPIO_NUM_3; interrupt.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), kTag, "touch IRQ input");
    g_touch_is_tt = address == 0x24;
    if (g_touch_is_tt) return g_tt21100.Init(g_bus);
#if defined(ASSISTANT_BOARD_BOX3)
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = address; io.scl_speed_hz = 400000; io.lcd_cmd_bits = 16; io.lcd_param_bits = 8;
    io.control_phase_bytes = 1; io.flags.disable_control_phase = true; io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), kTag, "GT911 IO");
    return g_gt911.Init(g_touch_io);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    bool pressed = false;
    uint16_t x = 0, y = 0;
    if (g_touch_is_tt) {
        Tt21100Frame frame{};
        result = g_tt21100.Read(&frame); pressed = frame.pressed; x = frame.x; y = frame.y;
    }
#if defined(ASSISTANT_BOARD_BOX3)
    else {
        Gt911Frame frame{};
        result = g_gt911.Read(&frame);
        if (result == ESP_OK && frame.count > 1) result = ESP_ERR_INVALID_RESPONSE;
        if (result == ESP_OK && frame.count == 1) { pressed = true; x = frame.points[0].x; y = frame.points[0].y; }
    }
#endif
    if (result != ESP_OK || (pressed && (x >= 320 || y >= 240))) {
        if (!g_touch_failed) ESP_LOGE(kTag, "touch canceled error=%s x=%u y=%u", esp_err_to_name(result), x, y);
        g_touch_failed = true; g_wait_release = true;
        lv_indev_reset(pointer, nullptr); lv_indev_wait_release(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(kTag, "touch recovered;waiting for release");
    g_touch_failed = false;
    if (!pressed) { g_wait_release = false; return; }
    if (g_wait_release) return;
    data->point.x = g_touch_is_tt ? 319 - x : x; data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}
#endif
}

esp_err_t box3_set_amplifier(bool enabled) {
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, enabled ? 1 : 0), kTag, "PA level");
    return gpio_get_level(GPIO_NUM_46) == (enabled ? 1 : 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t bsp_i2c_init() {
    if (g_bus_attempted) return g_bus ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_bus_attempted = true;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), kTag, "PA low latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_46), kTag, "PA hold release");
    gpio_config_t output = {};
    output.pin_bit_mask = 1ULL << GPIO_NUM_46; output.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "PA pad");
    ESP_RETURN_ON_ERROR(box3_set_amplifier(false), kTag, "PA off readback");
    ESP_RETURN_ON_ERROR(gpio_set_level(box_board::backlight, box_board::backlight_inverted ? 1 : 0), kTag, "backlight dark latch");
    output.pin_bit_mask = 1ULL << box_board::backlight; output.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "backlight pad");
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = spi_board::i2c_sda; bus.scl_io_num = spi_board::i2c_scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

lv_display_t *bsp_display_start() {
    if (g_display_attempted) return g_ready ? g_display : nullptr;
    g_display_attempted = true;
    esp_err_t result = bsp_i2c_init();
    if (result == ESP_OK) result = create_panel();
#if BSP_CAPS_TOUCH
    if (result == ESP_OK) result = create_touch();
#endif
    if (result == ESP_OK) {
        lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG(); port.task_affinity = 1; port.task_priority = 2;
        result = lvgl_port_init(&port);
    }
    if (result == ESP_OK) {
        if (!lvgl_port_lock(1000)) result = ESP_ERR_TIMEOUT;
        else {
            lvgl_port_display_cfg_t display = {};
            display.io_handle = g_io; display.panel_handle = g_panel; display.hres = 320; display.vres = 240;
            display.buffer_size = 320 * 20; display.double_buffer = true; display.color_format = LV_COLOR_FORMAT_RGB565;
            display.rotation.mirror_x = box_board::mirror_x; display.rotation.mirror_y = true;
            display.flags.buff_dma = true; display.flags.swap_bytes = true;
            g_display = lvgl_port_add_disp(&display);
#if BSP_CAPS_TOUCH
            if (g_display) {
                g_pointer = lv_indev_create();
                if (g_pointer) { lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER); lv_indev_set_display(g_pointer, g_display); lv_indev_set_read_cb(g_pointer, read_touch); }
            }
            result = g_display && g_pointer ? ESP_OK : ESP_ERR_NO_MEM;
#else
            result = g_display ? ESP_OK : ESP_ERR_NO_MEM;
#endif
            lvgl_port_unlock();
        }
    }
    if (result == ESP_OK) {
        ledc_timer_config_t timer = {};
        timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.duty_resolution = LEDC_TIMER_10_BIT;
        timer.timer_num = LEDC_TIMER_1; timer.freq_hz = 5000; timer.clk_cfg = LEDC_AUTO_CLK;
        result = ledc_timer_config(&timer);
        if (result == ESP_OK) {
            ledc_channel_config_t channel = {};
            channel.gpio_num = box_board::backlight; channel.speed_mode = LEDC_LOW_SPEED_MODE;
            channel.channel = LEDC_CHANNEL_1; channel.timer_sel = LEDC_TIMER_1;
            channel.duty = box_board::backlight_inverted ? 1023 : 0;
            result = ledc_channel_config(&channel);
            g_pwm_ready = result == ESP_OK;
        }
    }
    if (result != ESP_OK) {
        const esp_err_t dark = gpio_set_level(box_board::backlight, box_board::backlight_inverted ? 1 : 0);
        const esp_err_t mute = box3_set_amplifier(false);
        ESP_LOGE(kTag, "display stopped error=%s dark=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
        return nullptr;
    }
    g_ready = true;
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_pwm_ready) return ESP_ERR_INVALID_STATE;
    const int actual = box_board::backlight_inverted ? 100 - percent : percent;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, actual * 1023 / 100), kTag, "brightness duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

#if BSP_CAPS_SDCARD
esp_err_t bsp_sdcard_mount() {
    static sdmmc_card_t *card;
    if (card) return ESP_OK;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_43, 0), kTag, "dock SD power latch");
    gpio_config_t power = {}; power.pin_bit_mask = 1ULL << GPIO_NUM_43; power.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&power), kTag, "dock SD power");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_11; slot.cmd = GPIO_NUM_14;
    slot.d0 = GPIO_NUM_9; slot.d1 = GPIO_NUM_13; slot.d2 = GPIO_NUM_42; slot.d3 = GPIO_NUM_12; slot.width = 4;
    slot.flags = 0;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false; mount.max_files = 5; mount.allocation_unit_size = 16 * 1024;
    return esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot, &mount, &card);
}
#endif
