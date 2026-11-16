#include "cores3_board.h"
#include "ili9341_default_commands.h"
#include "i2c_register_bits.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *kTag = "cores3_board";
i2c_master_bus_handle_t g_bus;
i2c_master_dev_handle_t g_power, g_expander;
esp_lcd_panel_io_handle_t g_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
SemaphoreHandle_t g_power_lock;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_attempted, g_ready, g_display_attempted, g_display_ready, g_audio_ready, g_wait_release = true, g_touch_failed;

esp_err_t update(i2c_master_dev_handle_t device, uint8_t reg, uint8_t mask, uint8_t value) {
    return update_i2c_register_bits(device, reg, mask, value, 100, kTag);
}

esp_err_t initialize_ili9341(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x11, nullptr, 0), kTag, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x36, &axes, 1), kTag, "BGR");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, 0x3a, &format, 1), kTag, "RGB565");
    for (const auto &command : ILI9341_DEFAULT_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, command.code, command.data, command.length), kTag, "ILI9341 command%02x", command.code);
    }
    return ESP_OK;
}

esp_err_t sleep_ili9341(esp_lcd_panel_t *, bool sleeping) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_io, sleeping ? 0x10 : 0x11, nullptr, 0), kTag, "panel sleep");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    uint8_t packet[7] = {};
    // Preserve raw count/event bits: the generic helper clips contacts and
    // its empty-frame path does not necessarily clear a previous point.
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    const uint8_t count = packet[0] & 0x0f, event = packet[1] >> 6;
    const uint16_t x = ((packet[1] & 0x0f) << 8) | packet[2];
    const uint16_t y = ((packet[3] & 0x0f) << 8) | packet[4];
    if (result != ESP_OK || count > 1 || (count == 1 && (event == 3 || x >= 320 || y >= 240))) {
        if (!g_touch_failed) ESP_LOGE(kTag, "touch canceled error=%s count=%u event=%u xy=%u,%u", esp_err_to_name(result), count, event, x, y);
        g_touch_failed = true; g_wait_release = true;
        lv_indev_reset(pointer, nullptr); lv_indev_wait_release(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(kTag, "touch recovered;waiting for release");
    g_touch_failed = false;
    if (!count || event == 1) { g_wait_release = false; return; }
    if (g_wait_release) return;
    data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t create_display() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_37; bus.miso_io_num = GPIO_NUM_35; bus.sclk_io_num = GPIO_NUM_36;
    bus.quadwp_io_num = GPIO_NUM_NC; bus.quadhd_io_num = GPIO_NUM_NC; bus.max_transfer_sz = 320 * 20 * 2;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "shared SPI3");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_3; io.dc_gpio_num = GPIO_NUM_35; io.spi_mode = 0;
    io.pclk_hz = 40000000; io.lcd_cmd_bits = 8; io.lcd_param_bits = 8; io.trans_queue_depth = 10;
    // Current SDK5.5.6 enables DC output in pre_cb and disables it in post_cb.
    // GPIO35 can therefore remain the shared SD MISO between transactions.
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &g_io), kTag, "display IO");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_NC; panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR; panel.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_io, &panel, &g_panel), kTag, "ILI9341 DCS operations");
    g_panel->init = initialize_ili9341; g_panel->disp_sleep = sleep_ili9341; g_panel->set_brightness = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), kTag, "software reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), kTag, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), kTag, "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), kTag, "display on");
    esp_lcd_panel_io_i2c_config_t touch_io = {};
    touch_io.dev_addr = 0x38; touch_io.scl_speed_hz = 400000; touch_io.lcd_cmd_bits = 8; touch_io.lcd_param_bits = 8;
    touch_io.control_phase_bytes = 1; touch_io.flags.disable_control_phase = true; touch_io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &touch_io, &g_touch_io), kTag, "FT5x06 IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = 320; touch.y_max = 240; touch.rst_gpio_num = GPIO_NUM_NC; touch.int_gpio_num = GPIO_NUM_NC;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch), kTag, "FT5x06 init");
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG(); port.task_affinity = 1; port.task_priority = 2;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), kTag, "LVGL port");
    if (!lvgl_port_lock(1000)) return ESP_ERR_TIMEOUT;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_io; display.panel_handle = g_panel; display.hres = 320; display.vres = 240;
    display.buffer_size = 320 * 20; display.double_buffer = true; display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true; display.flags.swap_bytes = true;
    g_display = lvgl_port_add_disp(&display);
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) { lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER); lv_indev_set_display(g_pointer, g_display); lv_indev_set_read_cb(g_pointer, read_touch); }
    }
    lvgl_port_unlock();
    return g_display && g_pointer ? ESP_OK : ESP_ERR_NO_MEM;
}
}

esp_err_t bsp_i2c_init() {
    if (g_attempted) return g_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_attempted = true;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = spi_board::i2c_sda; bus.scl_io_num = spi_board::i2c_scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_bus), kTag, "I2C1");
    g_power_lock = xSemaphoreCreateMutex();
    if (!g_power_lock) return ESP_ERR_NO_MEM;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.scl_speed_hz = 400000; device.device_address = 0x34;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &device, &g_power), kTag, "AXP2101");
    device.device_address = 0x58;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &device, &g_expander), kTag, "AW9523");
    // Keep non-owned outputs, charger settings and the camera rail intact.
    // AXP: ALDO1=1.8V(AW88298), ALDO2=3.3V(ES7210). DLDO1 starts off.
    ESP_RETURN_ON_ERROR(update(g_power, 0x90, 0x80, 0), kTag, "backlight off");
    ESP_RETURN_ON_ERROR(update(g_power, 0x92, 0x1f, 13), kTag, "ALDO1 1.8V");
    ESP_RETURN_ON_ERROR(update(g_power, 0x93, 0x1f, 28), kTag, "ALDO2 3.3V");
    ESP_RETURN_ON_ERROR(update(g_power, 0x90, 0x03, 0x03), kTag, "audio rails");
    // Latch touch/codec/LCD reset low before configuring only these pins as
    // GPIO outputs. AW9523 GPIO mode bits1 mean GPIO, direction bits0 output.
    ESP_RETURN_ON_ERROR(update(g_expander, 0x02, 0x05, 0), kTag, "P0 touch/codec reset latch");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x03, 0x02, 0), kTag, "P1 LCD reset latch");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x11, 0x10, 0x10), kTag, "P0 push-pull");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x12, 0x05, 0x05), kTag, "P0 GPIO mode");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x13, 0x02, 0x02), kTag, "P1 GPIO mode");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x04, 0x05, 0), kTag, "P0 output direction");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x05, 0x02, 0), kTag, "P1 output direction");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(update(g_expander, 0x02, 0x01, 0x01), kTag, "touch reset release");
    ESP_RETURN_ON_ERROR(update(g_expander, 0x03, 0x02, 0x02), kTag, "LCD reset release");
    vTaskDelay(pdMS_TO_TICKS(50));
    g_ready = true;
    return ESP_OK;
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

esp_err_t cores3_prepare_audio() {
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), kTag, "power init");
    if (g_audio_ready) return ESP_OK;
    if (xSemaphoreTake(g_power_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update(g_expander, 0x02, 0x04, 0x04);
    xSemaphoreGive(g_power_lock);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(50));
    // This is the codec reset/enable, not a per-packet mute gate. Keep it high
    // across idle; actual mute goes through the AW88298 DAC callback.
    g_audio_ready = true;
    return ESP_OK;
}

lv_display_t *bsp_display_start() {
    if (g_display_attempted) return g_display_ready ? g_display : nullptr;
    g_display_attempted = true;
    esp_err_t result = bsp_i2c_init();
    if (result == ESP_OK) result = create_display();
    if (result != ESP_OK) {
        const esp_err_t dark = g_power ? update(g_power, 0x90, 0x80, 0) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(kTag, "display stopped error=%s dark=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark));
        return nullptr;
    }
    g_display_ready = true;
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready || !g_power_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_power_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result;
    if (!percent) result = update(g_power, 0x90, 0x80, 0);
    else {
        result = update(g_power, 0x99, 0x1f, 20 + 8 * percent / 100);
        if (result == ESP_OK) result = update(g_power, 0x90, 0x80, 0x80);
    }
    xSemaphoreGive(g_power_lock);
    return result;
}

esp_err_t bsp_sdcard_mount() {
    static sdmmc_card_t *card;
    if (card) return ESP_OK;
    if (!g_display_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_power_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = update(g_power, 0x95, 0x1f, 28);
    if (result == ESP_OK) result = update(g_power, 0x90, 0x08, 0x08);
    if (result == ESP_OK) result = update(g_expander, 0x02, 0x10, 0x10);
    if (result == ESP_OK) result = update(g_expander, 0x12, 0x10, 0x10);
    if (result == ESP_OK) result = update(g_expander, 0x04, 0x10, 0);
    xSemaphoreGive(g_power_lock);
    if (result != ESP_OK) return result;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT(); host.slot = SPI3_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT(); slot.host_id = SPI3_HOST; slot.gpio_cs = GPIO_NUM_4;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false; mount.max_files = 5; mount.allocation_unit_size = 16 * 1024;
    return esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, &host, &slot, &mount, &card);
}
