#include "k10_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "k10_board";
constexpr uint8_t kBacklight = 0x01, kKeyB = 0x04, kKeyA = 0x10, kAmplifierGain = 0x80;
i2c_master_dev_handle_t g_expander;
SemaphoreHandle_t g_lock;
uint8_t g_adc_address;
bool g_ready, g_started, g_key_error, g_wait_for_release = true;

esp_err_t read_register(uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(g_expander, &reg, 1, value, 1, 100);
}
}

esp_err_t k10_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus || g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x20;
    device.scl_speed_hz = 100000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &g_expander), TAG, "TCA9555at20");
    // ManufacturerinitBoard enum:P0LCD_BLK,P1cameraRESET,P15Amp_Gain;
    // manufacturerkeyA=P12/keyB=P2,oppositetothefixedXiaoZhi boardclass.
    // Own only backlight/gain/twoinputkeys. Do not pulse cameraRESET P1 or
    // reset the whole expander as the generic constructor would.
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x02, kBacklight, 0, 100, TAG), TAG, "backlight dark latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x03, kAmplifierGain, 0, 100, TAG), TAG, "manufacturer low amplifier gain");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x06, kBacklight | kKeyB, kKeyB, 100, TAG), TAG, "P0output/P2input");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x07, kAmplifierGain | kKeyA, kKeyA, 100, TAG), TAG, "P15gainoutput/P12input");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x04, kKeyB, 0, 100, TAG), TAG, "keyBinputpolarity");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x05, kKeyA, 0, 100, TAG), TAG, "keyAinputpolarity");
    // The pinned manufacturerlibmodules init_board tries physical0x15 then
    // physical0x11;its lowlevelwriter shifts each address left before I2C.
    // Probe only those documented candidates;timeouts are not absence.
    const esp_err_t primary = i2c_master_probe(bus, 0x15, 100);
    ESP_RETURN_ON_FALSE(primary == ESP_OK || primary == ESP_ERR_NOT_FOUND, primary, TAG, "ADC15probe");
    const esp_err_t secondary = i2c_master_probe(bus, 0x11, 100);
    ESP_RETURN_ON_FALSE(secondary == ESP_OK || secondary == ESP_ERR_NOT_FOUND, secondary, TAG, "ADC11probe");
    if ((primary == ESP_OK) == (secondary == ESP_OK)) {
        ESP_LOGE(TAG, "ADCidentity ambiguous/absent addr15=%s addr11=%s", esp_err_to_name(primary), esp_err_to_name(secondary));
        return ESP_ERR_INVALID_RESPONSE;
    }
    g_adc_address = primary == ESP_OK ? 0x15 : 0x11;
    g_ready = true;
    ESP_LOGI(TAG, "ready TCA9555owned P0BL P15gainLOW P12A/P2B inputs;ADC7bit=%02x API8bit=%02x;cameraP1preserved", g_adc_address, g_adc_address << 1);
    return ESP_OK;
}

uint8_t k10_adc_address() { return g_ready ? g_adc_address : 0; }

esp_err_t k10_set_backlight(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update_i2c_register_bits(g_expander, 0x02, kBacklight, enabled ? kBacklight : 0, 100, TAG);
    xSemaphoreGive(g_lock);
    return result;
}

ManualInputKey k10_read_keys() {
    if (!g_ready) return ManualInputKey::Invalid;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    uint8_t low = 0, high = 0;
    esp_err_t result = read_register(0x00, &low);
    if (result == ESP_OK) result = read_register(0x01, &high);
    xSemaphoreGive(g_lock);
    if (result != ESP_OK) {
        if (!g_key_error) ESP_LOGE(TAG, "keyread failed:%s", esp_err_to_name(result));
        g_key_error = true;
        g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    if (g_key_error) ESP_LOGI(TAG, "keys recovered;wait for release");
    g_key_error = false;
    const bool a = !(high & kKeyA), b = !(low & kKeyB);
    if (!a && !b) { g_wait_for_release = false; return ManualInputKey::None; }
    if ((a && b) || g_wait_for_release) { g_wait_for_release = true; return ManualInputKey::Invalid; }
    // Both physicalkeys provide the common talk/card workflow. Source's
    // longpressvolume gestures are not inferred from held talk input.
    return ManualInputKey::Listen;
}
