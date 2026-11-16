#include "stopwatch_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "stopwatch_board";
// M5IOE1 names are1-based: IO3=bit2,IO4=bit3,IO5=bit4,IO8=bit7,
// IO10=high-register bit1. Do not confuse IO9's motor with PA IO10.
constexpr uint8_t kCodec = 0x04, kTouchReset = 0x08, kLcdReset = 0x10, kLcdPower = 0x80;
constexpr uint8_t kLowOutputs = kCodec | kTouchReset | kLcdReset | kLcdPower;
constexpr uint8_t kAmplifier = 0x02;
i2c_master_dev_handle_t g_expander;
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t check_amplifier(bool enabled) {
    const uint8_t input_register = 0x08;
    uint8_t input = 0;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(g_expander, &input_register, 1, &input, 1, 100), TAG, "PA input readback");
    if (((input & kAmplifier) != 0) != enabled) {
        ESP_LOGE(TAG, "PA level mismatch expected=%d input=0x%02x", enabled, input);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
}

esp_err_t stopwatch_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_expander) {
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x4f;
        device.scl_speed_hz = 100000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &g_expander), TAG, "M5IOE1 device");
    }
    // Fixed1.0.9 driver accepts REV 'W' at4F/50. Report UID without
    // inventing a UID whitelist; stop an incompatible revision before writes.
    const uint8_t uid_register = 0;
    uint8_t identity[3]{};
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(g_expander, &uid_register, 1, identity, sizeof(identity), 100), TAG, "M5IOE1 identity");
    if (identity[2] != 'W') {
        ESP_LOGE(TAG, "M5IOE1 unsupported revision=0x%02x", identity[2]);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "M5IOE1 uid=0x%02x%02x revision=W", identity[1], identity[0]);
    // Disable PWM2 onIO8 and PWM4 onIO10 before GPIO ownership. Other PWM
    // channels, motorIO9, all unrelated pulls and PM1 charger/boost stay owned
    // by their existing configuration. No vendor begin/reset or PM1 writes.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x1e, 0x80, 0, 100, TAG), TAG, "LCD power GPIO mode");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x22, 0x80, 0, 100, TAG), TAG, "PA GPIO mode");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x06, kAmplifier, 0, 100, TAG), TAG, "PA off latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x05, kLowOutputs, kCodec | kLcdPower, 100, TAG), TAG, "power/reset latches");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x09, kLowOutputs, 0, 100, TAG), TAG, "owned pullup disable");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x0b, kLowOutputs, 0, 100, TAG), TAG, "owned pulldown disable");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x0a, kAmplifier, 0, 100, TAG), TAG, "PA pullup disable");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x0c, kAmplifier, 0, 100, TAG), TAG, "PA pulldown disable");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x13, kLowOutputs, 0, 100, TAG), TAG, "owned pushpull");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x14, kAmplifier, 0, 100, TAG), TAG, "PA pushpull");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x03, kLowOutputs, kLowOutputs, 100, TAG), TAG, "owned output directions");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x04, kAmplifier, kAmplifier, 100, TAG), TAG, "PA output direction");
    ESP_RETURN_ON_ERROR(check_amplifier(false), TAG, "PA off confirmation");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x05, kLcdReset, kLcdReset, 100, TAG), TAG, "LCD reset release");
    vTaskDelay(pdMS_TO_TICKS(120));
    // The fixed board never initializes touch. Add the documented CST820B
    // external reset path; its family driver must not issue a second GPIO reset.
    vTaskDelay(pdMS_TO_TICKS(70));
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x05, kTouchReset, kTouchReset, 100, TAG), TAG, "touch reset release");
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_config_t keys = {};
    keys.pin_bit_mask = (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_1);
    keys.mode = GPIO_MODE_INPUT;
    keys.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&keys), TAG, "user keys");
    g_ready = true;
    ESP_LOGI(TAG, "LCD/codec rails ready, PAoff, LCDreset10-120ms touchreset200-200ms; PM1/motor configuration preserved");
    return ESP_OK;
}

esp_err_t stopwatch_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = update_i2c_register_bits(g_expander, 0x06, kAmplifier, enabled ? kAmplifier : 0, 100, TAG);
    if (result == ESP_OK) result = check_amplifier(enabled);
    xSemaphoreGive(g_lock);
    return result;
}

ManualInputKey stopwatch_read_keys() {
    const bool talk = !gpio_get_level(GPIO_NUM_2), volume = !gpio_get_level(GPIO_NUM_1);
    static bool released = false;
    static int source = 0;
    if (!talk && !volume) { released = true; source = 0; return ManualInputKey::None; }
    const int current = talk ? 1 : 2;
    if ((talk && volume) || (source && source != current)) released = false;
    if (!released) return ManualInputKey::Invalid;
    source = current;
    return talk ? ManualInputKey::Listen : ManualInputKey::VolumeUp;
}
