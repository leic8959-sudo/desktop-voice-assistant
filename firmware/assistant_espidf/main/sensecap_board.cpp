#include "sensecap_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "sensecap_board";
// High port bits: P1.1 LCD, P1.2 system latch, P1.4 speaker PA.
constexpr uint8_t kLcd = 0x02, kSystem = 0x04, kPa = 0x10, kKey = 0x08;
i2c_master_bus_handle_t g_bus;
i2c_master_dev_handle_t g_expander;
SemaphoreHandle_t g_lock;
bool g_started, g_ready, g_wait_for_release = true, g_key_error;
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0; bus.sda_io_num = GPIO_NUM_47; bus.scl_io_num = GPIO_NUM_48;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

esp_err_t sensecap_board_init() {
    if (g_ready) return ESP_OK;
    if (g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_8, 0), TAG, "backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_8; backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight dark pad");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C0");
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.device_address = 0x21; device.scl_speed_hz = 100000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &device, &g_expander), TAG, "TCA9555at21");
    // Preload only owned output bits before switching directions. Never write
    // the source's whole-port DF00/FF00 startup mask (AI/SD/Grove/ADC rails).
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x03, kLcd | kSystem | kPa, kSystem, 100, TAG), TAG, "system held LCD/PA inactive");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x07, kLcd | kSystem | kPa, 0, 100, TAG), TAG, "owned output directions");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x06, kKey, kKey, 100, TAG), TAG, "knob push input");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x04, kKey, 0, 100, TAG), TAG, "knob push normal polarity");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x03, kLcd, kLcd, 100, TAG), TAG, "LCD supply on");
    vTaskDelay(pdMS_TO_TICKS(50));
    // The schematic fixes ADC0x14 and DAC0x18; do not probe K10 alternatives.
    ESP_RETURN_ON_ERROR(i2c_master_probe(g_bus, 0x14, 100), TAG, "ES7243E14 ACK");
    ESP_RETURN_ON_ERROR(i2c_master_probe(g_bus, 0x18, 100), TAG, "ES831118 ACK");
    g_ready = true;
    ESP_LOGI(TAG, "ready:onlyP9LCD P10system P12PA P3key owned;AI/SD/Grove/ADC preserved");
    return ESP_OK;
}

esp_err_t sensecap_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_expander, 0x03, kPa, enabled ? kPa : 0, 100, TAG);
    xSemaphoreGive(g_lock);
    return result;
}

ManualInputKey sensecap_read_key() {
    if (!g_ready) return ManualInputKey::Invalid;
    uint8_t inputs = 0;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) { g_wait_for_release = true; return ManualInputKey::Invalid; }
    const uint8_t reg = 0;
    const esp_err_t result = i2c_master_transmit_receive(g_expander, &reg, 1, &inputs, 1, 100);
    xSemaphoreGive(g_lock);
    if (result != ESP_OK) {
        if (!g_key_error) ESP_LOGE(TAG, "knob push read failed:%s", esp_err_to_name(result));
        g_key_error = true; g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    if (g_key_error) ESP_LOGI(TAG, "knob push recovered;wait for release");
    g_key_error = false;
    if (inputs & kKey) { g_wait_for_release = false; return ManualInputKey::None; }
    return g_wait_for_release ? ManualInputKey::Invalid : ManualInputKey::Listen;
}
