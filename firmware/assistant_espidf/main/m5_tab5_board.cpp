#include "m5_tab5_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "m5_tab5_board";
i2c_master_bus_handle_t g_i2c;
i2c_master_dev_handle_t g_io1, g_io2;
SemaphoreHandle_t g_lock;
bool g_ready, g_attempted;

esp_err_t update(i2c_master_dev_handle_t device, uint8_t reg, uint8_t mask, uint8_t value) {
    return update_i2c_register_bits(device, reg, mask, value, 100, TAG);
}

esp_err_t initialize() {
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_22, 0), TAG, "backlight off latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_22; backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight pad");
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = GPIO_NUM_31; bus.scl_io_num = GPIO_NUM_32;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_i2c), TAG, "system I2C1");
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.scl_speed_hz = 100000;
    device.device_address = 0x43;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &device, &g_io1), TAG, "PI4IOE1");
    device.device_address = 0x44;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &device, &g_io2), TAG, "PI4IOE2");
    // PI4IOE direction is 1=output. Own internal-antenna0/SPK_EN1/LCD_RST4/TP_RST5
    // and WLAN_PWR_EN0; preserve camera, USB, external5V and charge bits.
    ESP_RETURN_ON_ERROR(update(g_io1, 0x05, 0x33, 0), TAG, "internal antenna/PA off/reset low latch");
    ESP_RETURN_ON_ERROR(update(g_io1, 0x03, 0x33, 0x33), TAG, "antenna/PA/reset output mode");
    ESP_RETURN_ON_ERROR(update(g_io1, 0x07, 0x33, 0), TAG, "antenna/PA/reset disable high impedance");
    ESP_RETURN_ON_ERROR(update(g_io2, 0x05, 0x01, 0x01), TAG, "WLAN power latch");
    ESP_RETURN_ON_ERROR(update(g_io2, 0x03, 0x01, 0x01), TAG, "WLAN output mode");
    return update(g_io2, 0x07, 0x01, 0);
}
}

esp_err_t m5_tab5_board_init() {
    if (g_attempted) return g_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_attempted = true;
    const esp_err_t result = initialize();
    g_ready = result == ESP_OK;
    if (!g_ready) {
        const esp_err_t dark = gpio_set_level(GPIO_NUM_22, 0);
        const esp_err_t mute = g_io1 ? update(g_io1, 0x05, 0x02, 0) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "startup stopped:%s dark=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
    }
    return result;
}

esp_err_t m5_tab5_set_amplifier(bool enabled) {
    if (!g_ready || !g_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update(g_io1, 0x05, 0x02, enabled ? 0x02 : 0);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t m5_tab5_reset_display() {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = gpio_reset_pin(GPIO_NUM_23);
    if (result == ESP_OK) result = update(g_io1, 0x05, 0x30, 0);
    if (result == ESP_OK) result = update(g_io1, 0x03, 0x30, 0x30);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        // Manufacturer Tab5 notes require LCD_RST to release as input pull-up
        // for ST panels; driving it high can disturb their shared I2C bus.
        result = update(g_io1, 0x0D, 0x10, 0x10);
        if (result == ESP_OK) result = update(g_io1, 0x0B, 0x10, 0x10);
        if (result == ESP_OK) result = update(g_io1, 0x03, 0x10, 0);
        if (result == ESP_OK) result = update(g_io1, 0x05, 0x30, 0x30);
        if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
    }
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t bsp_i2c_init() { return m5_tab5_board_init(); }
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }
