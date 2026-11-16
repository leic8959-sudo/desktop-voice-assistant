#include "mvsrlora_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "mvsrlora_board";
SemaphoreHandle_t g_lock;
i2c_master_dev_handle_t g_touch;
bool g_ready, g_started;

esp_err_t set_amplifier(bool enabled) {
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_41, enabled ? 1 : 0), TAG, "MAX boost enable41");
    vTaskDelay(pdMS_TO_TICKS(1));
    if (gpio_get_level(GPIO_NUM_41) != (enabled ? 1 : 0)) {
        ESP_LOGE(TAG, "MAX enable input mismatch enabled=%d", enabled);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
}

esp_err_t mvsrlora_board_init() {
    if (g_ready) return ESP_OK;
    if (g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    // Hardware facts from the pinned manufacturer schematic. MIC3 controls
    // two P-channel gates(activeLOW);MAX41 controls its boost(activeHIGH).
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, 0), TAG, "backlight dark latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_3, 1), TAG, "MIC off while supply settles");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_42, 1), TAG, "RT9080 rail enable latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_14, 1), TAG, "shared SPI SD deselect");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_7, 1), TAG, "shared SPI LR1121 deselect");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << GPIO_NUM_48) | (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_42) |
        (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_7);
    outputs.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "startup outputs");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_41, 0), TAG, "MAX off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_41;
    amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), TAG, "MAX enable output/input readback");
    ESP_RETURN_ON_ERROR(set_amplifier(false), TAG, "MAX off verified");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_3, 0), TAG, "MIC pair supply on");
    vTaskDelay(pdMS_TO_TICKS(10));
    // GPIO21 is a shared interrupt line(touch/charger/RTC/IMU). Poll touch;
    // never attach an exclusive touch IRQ handler or drive this net.
    gpio_config_t interrupt = {};
    interrupt.pin_bit_mask = 1ULL << GPIO_NUM_21;
    interrupt.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&interrupt), TAG, "shared interrupt input");
    // Manufacturer CST226 sequence;keep all other peripheral resets unowned.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_13, 1), TAG, "touch reset inactive latch");
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << GPIO_NUM_13;
    reset.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "touch reset output");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_13, 0), TAG, "touch reset assert");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_13, 1), TAG, "touch reset release");
    vTaskDelay(pdMS_TO_TICKS(200));
    g_ready = true;
    ESP_LOGI(TAG, "ready RT9080on MICpairon MAXoff SD-LoRAdeselected;motor/charger/RTC/LoRa radio untouched");
    return ESP_OK;
}

esp_err_t mvsrlora_set_amplifier(bool enabled) {
    if (!g_ready || !g_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = set_amplifier(enabled);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t mvsrlora_touch_init(i2c_master_bus_handle_t bus) {
    if (!g_ready || !bus || g_touch) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x5a, 100), TAG, "CST226SE presence");
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x5a;
    device.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &device, &g_touch);
}

// CST226 report layout checked against SensorLib@43120dee04f3054e2ce4fe9715e46a82557dee2e
// TouchDrvCST226.cpp(MIT,Copyright(c)2023lewis he) and TouchLib CST register notes.
// Modified:bounded single-point packet/checked sync/error propagation/no dynamic
// point list. Original terms:third_party/mvsrlora_touch_protocol/LICENSE.
esp_err_t mvsrlora_read_touch(bool *pressed, uint16_t *x, uint16_t *y) {
    if (!pressed || !x || !y || !g_touch) return ESP_ERR_INVALID_ARG;
    *pressed = false;
    *x = *y = 0;
    const uint8_t status = 0x00;
    uint8_t packet[7]{};
    esp_err_t result = i2c_master_transmit_receive(g_touch, &status, 1, packet, sizeof(packet), 100);
    if (result != ESP_OK) return result;
    // Acknowledge only a successfully read snapshot. A failed sync cancels
    // the input,so the same stale report cannot be accepted as a fresh click.
    const uint8_t sync[] = {0x00, 0xab};
    result = i2c_master_transmit(g_touch, sync, sizeof(sync), 100);
    if (result != ESP_OK) return result;
    if (packet[6] != 0xab || (packet[5] & 0x80)) return ESP_ERR_INVALID_RESPONSE;
    const uint8_t count = packet[5] & 0x7f;
    if (count > 1) return ESP_ERR_INVALID_RESPONSE;
    if (!count) return ESP_OK;
    // A report that merely echoes the sync token is not proof of release.
    // Require a framed zero-contact report before the UI can rearm a press.
    if (packet[0] == 0x00 || packet[0] == 0xab) return ESP_ERR_INVALID_RESPONSE;
    if (count != 1 || (packet[0] & 0x0f) != 0x06) return ESP_ERR_INVALID_RESPONSE;
    *x = (static_cast<uint16_t>(packet[1]) << 4) | (packet[3] >> 4);
    *y = (static_cast<uint16_t>(packet[2]) << 4) | (packet[3] & 0x0f);
    *pressed = true;
    return ESP_OK;
}
