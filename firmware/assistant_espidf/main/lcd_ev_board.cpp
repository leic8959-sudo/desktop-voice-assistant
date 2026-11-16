#include "lcd_ev_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "lcd_ev_board";
constexpr uint8_t kAmplifier = 0x01;
#if defined(ASSISTANT_BOARD_LCD_EV_2)
constexpr uint8_t kOwnedPins = kAmplifier;
#else
constexpr uint8_t kOwnedPins = 0x0f;
#endif
i2c_master_dev_handle_t g_device;
esp_io_expander_t g_expander{};
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t read_register(uint8_t reg, uint32_t *value) {
    uint8_t actual = 0;
    const esp_err_t result = i2c_master_transmit_receive(g_device, &reg, 1, &actual, 1, 100);
    if (result != ESP_OK) ESP_LOGE(TAG, "read_failed reg=0x%02x error=%s", reg, esp_err_to_name(result));
    if (result == ESP_OK) *value = actual;
    return result;
}

// The installed TCA9554 constructor resets the entire port. This small public
// expander adapter lets the existing3wireIO own only P1/2/3, preserving P4..7.
// All callbacks read the actual registers rather than software caches.
esp_err_t read_input(esp_io_expander_handle_t, uint32_t *value) { return read_register(0x00, value); }
esp_err_t read_output(esp_io_expander_handle_t, uint32_t *value) { return read_register(0x01, value); }
esp_err_t read_direction(esp_io_expander_handle_t, uint32_t *value) { return read_register(0x03, value); }
esp_err_t write_output(esp_io_expander_handle_t, uint32_t value) {
    return update_i2c_register_bits(g_device, 0x01, kOwnedPins, value, 100, TAG);
}
esp_err_t write_direction(esp_io_expander_handle_t, uint32_t value) {
    return update_i2c_register_bits(g_device, 0x03, kOwnedPins, value, 100, TAG);
}

esp_err_t set_amplifier(bool enabled) {
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01, kAmplifier, enabled ? kAmplifier : 0, 100, TAG), TAG, "PA latch");
    uint32_t input = 0;
    ESP_RETURN_ON_ERROR(read_register(0x00, &input), TAG, "PA physical input");
    if ((input & kAmplifier) != (enabled ? kAmplifier : 0)) {
        ESP_LOGE(TAG, "PA level mismatch enabled=%d input=0x%02lx", enabled, static_cast<unsigned long>(input));
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
}

esp_err_t lcd_ev_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus || g_device) return ESP_ERR_INVALID_STATE;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x20;
    device.scl_speed_hz = 100000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &g_device), TAG, "TCA9554 at20");
    // Preload PA low before enabling its output. CS starts inactive/high;
    // the3wirecomponent controls P1/2/3 only while display startup runs.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x01, kOwnedPins,
        kOwnedPins & 0x02, 100, TAG), TAG, "safe startup latches");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x02, kAmplifier, 0, 100, TAG), TAG, "PA input polarity");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_device, 0x03, kOwnedPins, 0, 100, TAG), TAG, "owned outputs");
    ESP_RETURN_ON_ERROR(set_amplifier(false), TAG, "PA off verified");
    g_expander.read_input_reg = read_input;
    g_expander.read_output_reg = read_output;
    g_expander.write_output_reg = write_output;
    g_expander.read_direction_reg = read_direction;
    g_expander.write_direction_reg = write_direction;
    g_expander.config.io_count = 8;
    g_expander.config.flags.dir_out_bit_zero = true;
    g_ready = true;
    ESP_LOGI(TAG, "TCA9554 ready owned_mask=0x%02x PAoff;unowned registers preserved", kOwnedPins);
    return ESP_OK;
}

esp_io_expander_handle_t lcd_ev_get_expander() { return g_ready ? &g_expander : nullptr; }

esp_err_t lcd_ev_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    // Startup3wireIO is deleted before audio starts. Runtime PA writers are
    // serialized here and only change P0 with latch and physical readback.
    const esp_err_t result = set_amplifier(enabled);
    xSemaphoreGive(g_lock);
    return result;
}
