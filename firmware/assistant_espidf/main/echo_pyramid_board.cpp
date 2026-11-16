#include "echo_pyramid_board.h"

#include <cstring>
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "echo_pyramid";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_clock, g_controller, g_amplifier;
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t add_device(i2c_master_bus_handle_t bus, int address, i2c_master_dev_handle_t *device) {
    if (*device) return ESP_OK;
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &config, device);
}

esp_err_t check_amplifier_id() {
    const uint8_t reg = 0x00;
    uint8_t identifier = 0;
    esp_err_t result = i2c_master_transmit_receive(g_amplifier, &reg, 1, &identifier, 1, kTimeoutMs);
    if (result == ESP_OK && identifier != 0x5a) result = ESP_ERR_INVALID_RESPONSE;
    if (result != ESP_OK) ESP_LOGE(TAG, "AW87559_ID_failed value=0x%02x error=%s", identifier, esp_err_to_name(result));
    return result;
}

esp_err_t write_clock_block(uint8_t start, const uint8_t (&data)[8]) {
    uint8_t packet[9] = {start}, actual[8] = {};
    std::memcpy(packet + 1, data, sizeof(data));
    esp_err_t result = i2c_master_transmit(g_clock, packet, sizeof(packet), kTimeoutMs);
    if (result == ESP_OK) result = i2c_master_transmit_receive(g_clock, &start, 1, actual, sizeof(actual), kTimeoutMs);
    if (result == ESP_OK && std::memcmp(actual, data, sizeof(data)) != 0) result = ESP_ERR_INVALID_RESPONSE;
    if (result != ESP_OK) ESP_LOGE(TAG, "clock_block_failed register=%u error=%s", start, esp_err_to_name(result));
    return result;
}

esp_err_t initialize_clock() {
    // 27MHz * (32 +96/125) /144 =6.144MHz, exactly256 *24kHz.
    // PLLA P1=3682/P2=38/P3=125; MS1 P1=17920/P2=0/P3=1.
    // Reduced fractions avoid the pinned source's32-bit rest*1000000 overflow.
    constexpr uint8_t pll_a[8] = {0x00, 0x7d, 0x00, 0x0e, 0x62, 0x00, 0x00, 0x26};
    constexpr uint8_t multisynth_1[8] = {0x00, 0x01, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00};
    // This dedicated board uses CLK1 only, matching its owning source. The
    // other outputs stay disabled; this is not a general Si5351 clock service.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 3, 0xff, 0xff, kTimeoutMs, TAG), TAG, "disable clocks");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 16, 0xff, 0x80, kTimeoutMs, TAG), TAG, "CLK0 power down");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 17, 0xff, 0x80, kTimeoutMs, TAG), TAG, "CLK1 power down");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 18, 0xff, 0x80, kTimeoutMs, TAG), TAG, "CLK2 power down");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 183, 0xc0, 0xc0, kTimeoutMs, TAG), TAG, "27MHz crystal load");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 15, 0x04, 0, kTimeoutMs, TAG), TAG, "PLLA crystal source");
    ESP_RETURN_ON_ERROR(write_clock_block(26, pll_a), TAG, "PLLA parameters");
    ESP_RETURN_ON_ERROR(write_clock_block(50, multisynth_1), TAG, "MS1 parameters");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_clock, 17, 0xff, 0x4f, kTimeoutMs, TAG), TAG, "CLK1 PLLA integer/8mA");
    // Reset is self-clearing: write only PLLA reset, never compare its latch.
    const uint8_t reset[] = {177, 0x20};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_clock, reset, sizeof(reset), kTimeoutMs), TAG, "PLLA reset");
    const int64_t deadline = esp_timer_get_time() + 500000;
    const uint8_t status_register = 0;
    uint8_t status = 0xff;
    do {
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(g_clock, &status_register, 1, &status, 1, kTimeoutMs), TAG, "Si5351 lock status");
        // SYS_INIT, LOL_A and LOS must clear. PLLB is unused by CLK1.
        if ((status & 0xb0) == 0) return update_i2c_register_bits(g_clock, 3, 0xff, 0xfd, kTimeoutMs, TAG);
    } while (esp_timer_get_time() < deadline);
    ESP_LOGE(TAG, "Si5351_not_locked status=0x%02x", status);
    return ESP_ERR_TIMEOUT;
}
}

esp_err_t echo_pyramid_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    // Probe only the five required devices. Missing devices get a bounded
    // startup retry; a real bus failure is not treated as device absence.
    constexpr uint8_t addresses[] = {0x18, 0x40, 0x60, 0x1a, 0x5b};
    esp_err_t result = ESP_ERR_NOT_FOUND;
    for (int attempt = 0; attempt < 20; ++attempt) {
        for (const uint8_t address : addresses) {
            result = i2c_master_probe(bus, address, kTimeoutMs);
            if (result != ESP_OK) break;
        }
        if (result == ESP_OK) break;
        if (result != ESP_ERR_NOT_FOUND) return result;
        if (attempt != 19) vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_RETURN_ON_ERROR(result, TAG, "Pyramid required devices");
    ESP_RETURN_ON_ERROR(add_device(bus, 0x60, &g_clock), TAG, "Si5351 device");
    ESP_RETURN_ON_ERROR(add_device(bus, 0x1a, &g_controller), TAG, "STM32 device");
    ESP_RETURN_ON_ERROR(add_device(bus, 0x5b, &g_amplifier), TAG, "AW87559 device");
    ESP_RETURN_ON_ERROR(check_amplifier_id(), TAG, "AW87559 before startup");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_amplifier, 0x01, 0x08, 0, kTimeoutMs, TAG), TAG, "initial PA off");
    ESP_RETURN_ON_ERROR(initialize_clock(), TAG, "external24kHz MCLK");
    // Documented speaker reset only. Do not initialize STM32 RGB pages.
    const uint8_t reset_speaker[] = {0xa0, 1};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_controller, reset_speaker, sizeof(reset_speaker), kTimeoutMs), TAG, "STM32 speaker reset");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(check_amplifier_id(), TAG, "AW87559 after reset");
    // Keep SW/boost enabled and PA disabled until a checked codec unmute.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_amplifier, 0x01, 0x58, 0x50, kTimeoutMs, TAG), TAG, "AW87559 standby");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_amplifier, 0x06, 0x1f, 11, kTimeoutMs, TAG), TAG, "AW87559 gain16.5dB");
    g_ready = true;
    ESP_LOGI(TAG, "Pyramid_ready MCLK=6144000Hz CLK1 PA=off reference=software");
    return ESP_OK;
}

esp_err_t echo_pyramid_set_amplifier(bool enabled) {
    // A reset write may have reached the STM32 even if its ACK timed out.
    // Allow the startup error path to cut PA before full initialization, but
    // recheck the actual amplifier ID first. Enabling still requires ready.
    if (!g_lock || !g_amplifier || (enabled && !g_ready)) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = g_ready ? ESP_OK : check_amplifier_id();
    if (result == ESP_OK) result = update_i2c_register_bits(g_amplifier, 0x01, 0x08, enabled ? 0x08 : 0, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t echo_pyramid_audio_status() {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const uint8_t reg = 0;
    uint8_t status = 0xff;
    esp_err_t result = i2c_master_transmit_receive(g_clock, &reg, 1, &status, 1, kTimeoutMs);
    if (result == ESP_OK && (status & 0xb0)) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = check_amplifier_id();
    if (result != ESP_OK) ESP_LOGE(TAG, "audio_clock_or_PA_failed status=0x%02x error=%s", status, esp_err_to_name(result));
    xSemaphoreGive(g_lock);
    return result;
}
