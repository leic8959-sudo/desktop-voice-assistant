#include "atomic_echo_base.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "atomic_echo_base";
constexpr int kTimeoutMs = 100;
i2c_master_dev_handle_t g_expander;
SemaphoreHandle_t g_lock;
bool g_ready;
}

esp_err_t atomic_echo_base_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    // Probe only the two actual base devices; never scan unrelated addresses.
    esp_err_t result = i2c_master_probe(bus, 0x18, kTimeoutMs);
    if (result == ESP_OK) result = i2c_master_probe(bus, 0x43, kTimeoutMs);
    if (result != ESP_OK) { ESP_LOGE(TAG, "base_probe_failed error=%s", esp_err_to_name(result)); return result; }
    if (!g_expander) {
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = 0x43;
        config.scl_speed_hz = 100000;
        result = i2c_master_bus_add_device(bus, &config, &g_expander);
        if (result != ESP_OK) return result;
    }
    const uint8_t id_register = 0x01;
    uint8_t identifier = 0;
    result = i2c_master_transmit_receive(g_expander, &id_register, 1, &identifier, 1, kTimeoutMs);
    if (result == ESP_OK && (identifier & 0xe0) != 0xa0) result = ESP_ERR_INVALID_RESPONSE;
    if (result != ESP_OK) { ESP_LOGE(TAG, "PI4IOE_ID_failed value=0x%02x error=%s", identifier, esp_err_to_name(result)); return result; }
    // Manufacturer schematic Sch_ECHO_Base_v1.0: only P0 drives NS4150B CTRL.
    // PI4IOE5V6408 DS40583 p9: direction03 P0=1 is output; high-Z07 P0=0 drives.
    // Set the mute latch first, then enable only P0. Preserve P1..P7, pulls,
    // reset and interrupt configuration rather than copying whole-port writes.
    result = update_i2c_register_bits(g_expander, 0x05, 0x01, 0x00, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_expander, 0x03, 0x01, 0x01, kTimeoutMs, TAG);
    if (result == ESP_OK) result = update_i2c_register_bits(g_expander, 0x07, 0x01, 0x00, kTimeoutMs, TAG);
    g_ready = result == ESP_OK;
    if (g_ready) ESP_LOGI(TAG, "base_ready codec18 expander43 amplifierP0=off");
    return result;
}

esp_err_t atomic_echo_base_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_expander, 0x05, 0x01, enabled ? 0x01 : 0x00, kTimeoutMs, TAG);
    xSemaphoreGive(g_lock);
    return result;
}
