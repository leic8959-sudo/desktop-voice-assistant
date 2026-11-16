#include "spot_c5_board.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char *kTag = "spot_c5";
constexpr gpio_num_t kMcuHold = GPIO_NUM_2, kPeripheral = GPIO_NUM_0, kPa = GPIO_NUM_23;
i2c_master_bus_handle_t g_bus;
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t retain_supply(gpio_num_t pin) {
    // Preload HIGH before releasing a hold inherited from software reset.
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, 1), kTag, "supply high latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(pin), kTag, "release old supply hold");
    gpio_config_t output = {};
    output.pin_bit_mask = 1ULL << pin; output.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "supply output/readback");
    if (!gpio_get_level(pin)) return ESP_ERR_INVALID_RESPONSE;
    return gpio_hold_en(pin);
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus) return ESP_OK;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0; bus.sda_io_num = GPIO_NUM_25; bus.scl_io_num = GPIO_NUM_26;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

esp_err_t spot_c5_board_init() {
    if (g_ready) return ESP_OK;
    ESP_RETURN_ON_ERROR(retain_supply(kMcuHold), kTag, "MCU supply retention");
    ESP_RETURN_ON_ERROR(gpio_set_level(kPa, 0), kTag, "PA off latch");
    gpio_config_t pa = {};
    pa.pin_bit_mask = 1ULL << kPa; pa.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&pa), kTag, "PA readback pad");
    if (gpio_get_level(kPa)) return ESP_ERR_INVALID_RESPONSE;
    ESP_RETURN_ON_ERROR(retain_supply(kPeripheral), kTag, "peripheral supply retention");
    gpio_config_t key = {};
    key.pin_bit_mask = 1ULL << GPIO_NUM_5; key.mode = GPIO_MODE_INPUT;
    key.pull_down_en = GPIO_PULLDOWN_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&key), kTag, "active-high KEY5");
    key.pin_bit_mask = 1ULL << GPIO_NUM_28;
    key.pull_down_en = GPIO_PULLDOWN_DISABLE; key.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&key), kTag, "active-low BOOT28");
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), kTag, "codec I2C0");
    g_ready = true;
    return ESP_OK;
}

esp_err_t spot_c5_set_amplifier(bool enabled) {
    if (!g_ready || !g_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = gpio_set_level(kPa, enabled ? 1 : 0);
    if (result == ESP_OK && gpio_get_level(kPa) != (enabled ? 1 : 0)) result = ESP_ERR_INVALID_RESPONSE;
    xSemaphoreGive(g_lock);
    if (result != ESP_OK) ESP_LOGE(kTag, "PA update failed enabled=%d error=%s", enabled, esp_err_to_name(result));
    return result;
}
