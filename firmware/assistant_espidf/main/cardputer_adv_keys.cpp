#include "cardputer_adv_keys.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "cardputer_keys";
i2c_master_dev_handle_t g_keyboard;
uint64_t g_pressed;
bool g_ready, g_fault, g_wait_for_release = true;
int g_source;

esp_err_t read_register(uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(g_keyboard, &reg, 1, value, 1, 100);
}

esp_err_t read_events() {
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(read_register(0x02, &status), TAG, "keyboard status");
    if (status & 0x08) return ESP_ERR_INVALID_RESPONSE;
    // Hardware FIFO has ten entries. Bound each poll and never retry a
    // destructive FIFO read after an I2C failure: the consumed event is unknown.
    for (int index = 0; index < 10; ++index) {
        uint8_t count = 0;
        ESP_RETURN_ON_ERROR(read_register(0x03, &count), TAG, "keyboard event count");
        if (!(count & 0x0f)) break;
        uint8_t event = 0;
        ESP_RETURN_ON_ERROR(read_register(0x04, &event), TAG, "keyboard event FIFO");
        const uint8_t code = event & 0x7f;
        if (!code) return ESP_ERR_INVALID_RESPONSE;
        const int row = (code - 1) / 10, column = (code - 1) % 10;
        if (row >= 7 || column >= 8) return ESP_ERR_INVALID_RESPONSE;
        // Same7x8 ->4x14 mapping as the fixed owning driver. Keep all56 key
        // states so another key/modifier cannot turn a chord into a talk press.
        const int logical_row = column % 4;
        const int logical_column = row * 2 + (column > 3 ? 1 : 0);
        const uint64_t mask = 1ULL << (logical_row * 14 + logical_column);
        if (event & 0x80) g_pressed |= mask;
        else g_pressed &= ~mask;
    }
    ESP_RETURN_ON_ERROR(read_register(0x02, &status), TAG, "keyboard final status");
    if (status & 0x08) return ESP_ERR_INVALID_RESPONSE;
    // Clear only key interrupt. Never erase an overflow that may have arrived
    // between status reads. Polling does not depend on a fresh GPIO edge.
    const uint8_t clear[] = {0x02, 0x01};
    return i2c_master_transmit(g_keyboard, clear, sizeof(clear), 100);
}
}

esp_err_t cardputer_adv_keys_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x34;
    device.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &g_keyboard), TAG, "TCA8418 device");
    // Own the seven scan rows/eight columns. Leave unused GPIO bits alone.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_keyboard, 0x1d, 0x7f, 0x7f, 100, TAG), TAG, "keypad rows");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_keyboard, 0x1e, 0xff, 0xff, 100, TAG), TAG, "keypad columns");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_keyboard, 0x1f, 0x03, 0, 100, TAG), TAG, "unused columns");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_keyboard, 0x01, 0x31, 0x31, 100, TAG), TAG, "key event configuration");
    gpio_config_t interrupt = {};
    interrupt.pin_bit_mask = 1ULL << GPIO_NUM_11;
    interrupt.mode = GPIO_MODE_INPUT;
    // Board supplies external pull-ups. Use polling, no ISR/task allocation.
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "keyboard interrupt input");
    ESP_RETURN_ON_ERROR(read_events(), TAG, "initial keyboard state");
    g_ready = true;
    ESP_LOGI(TAG, "keyboard_ready Enter=talk/cards semicolon=volumeup period=volumedown; wait for release");
    return ESP_OK;
}

ManualInputKey cardputer_adv_read_keys() {
    if (!g_ready) return ManualInputKey::Invalid;
    if (!g_fault) {
        const esp_err_t result = read_events();
        if (result != ESP_OK) {
            g_fault = true;
            g_pressed = 0;
            g_wait_for_release = true;
            g_source = 0;
            ESP_LOGE(TAG, "keyboard_disabled error=%s; BOOT remains available after release; restart to retry keyboard", esp_err_to_name(result));
            return ManualInputKey::Invalid;
        }
    }
    const bool boot = !gpio_get_level(GPIO_NUM_0);
    if (!g_pressed && !boot) { g_wait_for_release = false; g_source = 0; return ManualInputKey::None; }
    constexpr uint64_t enter = 1ULL << (2 * 14 + 13);
    constexpr uint64_t up = 1ULL << (2 * 14 + 11);
    constexpr uint64_t down = 1ULL << (3 * 14 + 11);
    const bool single = g_pressed && !(g_pressed & (g_pressed - 1));
    const int source = boot ? 1 : g_pressed == enter ? 2 : g_pressed == up ? 3 : g_pressed == down ? 4 : 0;
    if ((!single && g_pressed) || (boot && g_pressed) || !source || (g_source && source != g_source)) g_wait_for_release = true;
    if (g_wait_for_release) return ManualInputKey::Invalid;
    g_source = source;
    return source <= 2 ? ManualInputKey::Listen : source == 3 ? ManualInputKey::VolumeUp : ManualInputKey::VolumeDown;
}
