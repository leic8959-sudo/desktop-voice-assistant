#include "c5_lcd169_board.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_cst816s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *kTag = "c5_lcd169";
constexpr uint8_t kPa = 1, kTouchReset = 2;
constexpr gpio_num_t kBacklight = GPIO_NUM_26;
i2c_master_bus_handle_t g_bus;
i2c_master_dev_handle_t g_expander;
SemaphoreHandle_t g_power_lock;
esp_lcd_panel_io_handle_t g_panel_io, g_touch_io;
esp_lcd_panel_handle_t g_panel;
esp_lcd_touch_handle_t g_touch;
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_ready, g_pwm_ready, g_display_attempted, g_wait_release = true, g_touch_failed;

esp_err_t verify_amplifier(bool enabled) {
    const uint8_t reg = 0;
    uint8_t input = 0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(g_expander, &reg, 1, &input, 1, 100), kTag, "TCA9554 PA input");
    return ((input & kPa) != 0) == enabled ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

void cancel_press(lv_indev_t *pointer) {
    lv_indev_reset(pointer, nullptr);
    lv_indev_wait_release(pointer);
    g_wait_release = true;
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    // Read the original count and event bits; the generic CST816S helper
    // clamps multiple contacts to one, which could turn ambiguity into a tap.
    uint8_t packet[5] = {};
    data->state = LV_INDEV_STATE_RELEASED;
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(kTag, "touch read failed=%s", esp_err_to_name(result));
        g_touch_failed = true;
        cancel_press(pointer);
        return;
    }
    g_touch_failed = false;
    const uint8_t contacts = packet[0] & 0x0f;
    const uint8_t event = packet[1] >> 6;
    if (contacts == 0 || (contacts == 1 && event == 1)) { g_wait_release = false; return; }
    const uint16_t x = ((packet[1] & 0x0f) << 8) | packet[2];
    const uint16_t y = ((packet[3] & 0x0f) << 8) | packet[4];
    if (contacts != 1 || event == 3 || x >= spi_board::width || y >= spi_board::height) {
        cancel_press(pointer);
        return;
    }
    if (g_wait_release) return;
    data->point.x = x; data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
}

esp_err_t initialize_panel() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = GPIO_NUM_7; bus.miso_io_num = GPIO_NUM_NC;
    bus.sclk_io_num = GPIO_NUM_23;
    bus.quadwp_io_num = GPIO_NUM_NC; bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = spi_board::width * 20 * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), kTag, "SPI2 display bus");
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = GPIO_NUM_27; io.dc_gpio_num = GPIO_NUM_24;
    io.spi_mode = 0; io.pclk_hz = 24000000;
    io.trans_queue_depth = 10; io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &g_panel_io), kTag, "display IO");
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = GPIO_NUM_NC;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; panel.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), kTag, "ST7789");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), kTag, "panel software reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), kTag, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), kTag, "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, false), kTag, "portrait axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), kTag, "portrait mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 20), kTag, "280-row window");
    return esp_lcd_panel_disp_on_off(g_panel, true);
}

esp_err_t initialize_touch() {
    esp_lcd_panel_io_i2c_config_t io = {};
    io.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS; io.scl_speed_hz = 400000;
    io.control_phase_bytes = 1; io.lcd_cmd_bits = 8;
    io.flags.disable_control_phase = true; io.transaction_timeout_ms = 100;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_bus, &io, &g_touch_io), kTag, "touch IO");
    esp_lcd_touch_config_t touch = {};
    touch.x_max = spi_board::width; touch.y_max = spi_board::height;
    touch.rst_gpio_num = GPIO_NUM_NC; touch.int_gpio_num = GPIO_NUM_5;
    touch.levels.reset = 0; touch.levels.interrupt = 0;
    return esp_lcd_touch_new_i2c_cst816s(g_touch_io, &touch, &g_touch);
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0; bus.sda_io_num = spi_board::i2c_sda; bus.scl_io_num = spi_board::i2c_scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

esp_err_t c5_lcd169_board_init() {
    if (g_ready) return ESP_OK;
    ESP_RETURN_ON_ERROR(gpio_set_level(kBacklight, 0), kTag, "backlight dark latch");
    gpio_config_t output = {};
    output.pin_bit_mask = 1ULL << kBacklight; output.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "backlight output");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), kTag, "I2C0");
    if (!g_power_lock) g_power_lock = xSemaphoreCreateMutex();
    if (!g_power_lock) return ESP_ERR_NO_MEM;
    if (!g_expander) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.device_address = 0x20; device.scl_speed_hz = 400000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &device, &g_expander), kTag, "TCA9554");
    }
    // Latch PA/reset low before enabling outputs. Keep all unowned expander
    // pins intact; do not inherit the vendor's default amplifier-on state.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 1, kPa | kTouchReset, 0, 100, kTag), kTag, "PA off/reset low latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 2, kPa | kTouchReset, 0, 100, kTag), kTag, "owned input polarity");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 3, kPa | kTouchReset, 0, 100, kTag), kTag, "PA/reset directions");
    ESP_RETURN_ON_ERROR(verify_amplifier(false), kTag, "PA off verification");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 1, kTouchReset, kTouchReset, 100, kTag), kTag, "touch reset release");
    vTaskDelay(pdMS_TO_TICKS(100));
    g_ready = true;
    return ESP_OK;
}

esp_err_t c5_lcd169_set_amplifier(bool enabled) {
    if (!g_ready || !g_power_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_power_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = update_i2c_register_bits(g_expander, 1, kPa, enabled ? kPa : 0, 100, kTag);
    if (result == ESP_OK) result = verify_amplifier(enabled);
    xSemaphoreGive(g_power_lock);
    return result;
}

lv_display_t *bsp_display_start() {
    if (g_display_attempted) return g_display && g_pointer && g_pwm_ready ? g_display : nullptr;
    g_display_attempted = true;
    if (c5_lcd169_board_init() != ESP_OK || initialize_panel() != ESP_OK || initialize_touch() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 0; port.task_priority = 2; // C5 is single-core.
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io; display.panel_handle = g_panel;
    display.hres = spi_board::width; display.vres = spi_board::height;
    display.buffer_size = spi_board::width * 20; display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
    display.flags.buff_dma = true; display.flags.swap_bytes = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
    if (g_display) {
        g_pointer = lv_indev_create();
        if (g_pointer) {
            lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(g_pointer, g_display);
            lv_indev_set_read_cb(g_pointer, read_touch);
        }
    }
    lvgl_port_unlock();
    if (!g_display || !g_pointer) return nullptr;
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0; timer.freq_hz = 5000; timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK) return nullptr;
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = kBacklight; backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0; backlight.timer_sel = LEDC_TIMER_0; backlight.duty = 0;
    if (ledc_channel_config(&backlight) != ESP_OK) return nullptr;
    g_pwm_ready = true;
    return g_display;
}

bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), kTag, "brightness duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
