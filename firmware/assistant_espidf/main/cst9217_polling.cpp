// Register/report contract adapted from Waveshare CST9217 driver, Apache-2.0.
// See third_party/waveshare_amoled216/README.md for the fixed source and license.
#include "cst9217_polling.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *kTag = "cst9217_polling";
constexpr int kTimeoutMs = 100;
}

esp_err_t Cst9217Polling::Reset() {
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_, 0), kTag, "touch reset low");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_, 1), kTag, "touch reset high");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

esp_err_t Cst9217Polling::ReadRegister(uint16_t reg, uint8_t *data, size_t length) {
    const uint8_t address[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    // The fixed driver selects the16-bit address in a separate transaction,
    // waits2ms, then receives. Keep that STOP boundary instead of guessing a
    // combined repeated-start transaction accepted by another touch model.
    const esp_err_t result = i2c_master_transmit(device_, address, sizeof(address), kTimeoutMs);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(2));
    return i2c_master_receive(device_, data, length, kTimeoutMs);
}

esp_err_t Cst9217Polling::Init(i2c_master_bus_handle_t bus, gpio_num_t reset, gpio_num_t interrupt) {
    if (attempted_) return ready_ ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!bus || !GPIO_IS_VALID_OUTPUT_GPIO(reset) || !GPIO_IS_VALID_GPIO(interrupt) || reset == interrupt) return ESP_ERR_INVALID_ARG;
    attempted_ = true; reset_ = reset;
    ESP_RETURN_ON_ERROR(gpio_set_level(reset, 1), kTag, "reset idle latch");
    gpio_config_t output = {};
    output.pin_bit_mask = 1ULL << reset; output.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&output), kTag, "reset output");
    gpio_config_t input = {};
    input.pin_bit_mask = 1ULL << interrupt; input.mode = GPIO_MODE_INPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&input), kTag, "touch interrupt input");
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.device_address = 0x5a; device.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &device_), kTag, "touch device");
    ESP_RETURN_ON_ERROR(Reset(), kTag, "initial reset");
    const uint8_t command[] = {0xd1, 0x01};
    // Preserve the fixed driver's address transaction and subsequent command
    // payload transaction, both D1-01. This is not an inferred CST9220 write.
    esp_err_t result = i2c_master_transmit(device_, command, sizeof(command), kTimeoutMs);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(2));
        result = i2c_master_transmit(device_, command, sizeof(command), kTimeoutMs);
    }
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t checkcode[4] = {}, resolution[4] = {}, identity[4] = {};
    if (result == ESP_OK) result = ReadRegister(0xd1fc, checkcode, sizeof(checkcode));
    if (result == ESP_OK) result = ReadRegister(0xd1f8, resolution, sizeof(resolution));
    if (result == ESP_OK) result = ReadRegister(0xd204, identity, sizeof(identity));
    // Hardware reset uses the original boot sequence to leave information
    // mode on either result. Do not introduce CST9220's unproven D109 command.
    const esp_err_t restored = Reset();
    if (result != ESP_OK || restored != ESP_OK) {
        ESP_LOGE(kTag, "touch setup failed read=%s reset=%s;restart required", esp_err_to_name(result), esp_err_to_name(restored));
        return result == ESP_OK ? restored : result;
    }
    const unsigned width = resolution[0] | (resolution[1] << 8);
    const unsigned height = resolution[2] | (resolution[3] << 8);
    ESP_LOGI(kTag, "legacy config chip=%02x%02x project=%02x%02x resolution=%ux%u check=%02x%02x%02x%02x;identity not authenticated",
        identity[3], identity[2], identity[1], identity[0], width, height, checkcode[3], checkcode[2], checkcode[1], checkcode[0]);
    // No verified chip-ID allowlist exists for this fixed driver. Keep the
    // observed identity in logs; report validation does not identify a chip.
    ready_ = true;
    return ESP_OK;
}

esp_err_t Cst9217Polling::Read(bool *pressed, uint16_t *x, uint16_t *y) {
    if (!pressed || !x || !y) return ESP_ERR_INVALID_ARG;
    *pressed = false; *x = 0; *y = 0;
    if (!ready_) return ESP_ERR_INVALID_STATE;
    uint8_t report[10] = {};
    const esp_err_t result = ReadRegister(0xd000, report, sizeof(report));
    if (result != ESP_OK) return result;
    if (report[6] != 0xab) return ESP_ERR_INVALID_RESPONSE;
    const uint8_t count = report[5] & 0x7f;
    if (count == 0) return ESP_OK;
    if (count != 1) return ESP_ERR_INVALID_RESPONSE;
    const uint8_t status = report[0] & 0x0f;
    if (status == 0) return ESP_OK;
    if (status != 6) return ESP_ERR_INVALID_RESPONSE;
    *x = (report[1] << 4) | (report[3] >> 4);
    *y = (report[2] << 4) | (report[3] & 0x0f);
    *pressed = true;
    return ESP_OK;
}
