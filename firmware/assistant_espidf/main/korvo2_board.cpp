#include "korvo2_board.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

namespace {
constexpr const char *TAG = "korvo2_board";
constexpr int kTimeoutMs = 100;
// Official BSP maps virtual GPIO50/51 from GPIO_NUM_MAX49: P1/P2.
// P0 is not LCD backlight. Preserve it and every unused expander pin.
static_assert(GPIO_NUM_MAX == 49, "Recheck Korvo2 virtual GPIO mapping");
constexpr uint8_t kBacklight = 0x02, kReset = 0x04, kSelect = 0x08;
i2c_master_dev_handle_t g_expander;
adc_oneshot_unit_handle_t g_adc;
adc_cali_handle_t g_calibration;
SemaphoreHandle_t g_lock;
bool g_ready, g_wait_for_release = true;
ManualInputKey g_previous_key = ManualInputKey::None;
esp_err_t g_last_key_error = ESP_OK;

esp_err_t set_output(uint8_t mask, uint8_t bits) {
    return update_i2c_register_bits(g_expander, 0x01, mask, bits, kTimeoutMs, TAG);
}
}

esp_err_t korvo2_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (!g_expander) {
        int address = 0x20;
        esp_err_t result = i2c_master_probe(bus, address, kTimeoutMs);
        if (result == ESP_ERR_NOT_FOUND) {
            address = 0x38;
            result = i2c_master_probe(bus, address, kTimeoutMs);
        }
        ESP_RETURN_ON_ERROR(result, TAG, "TCA9554/9554A probe");
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = address;
        device.scl_speed_hz = 400000;
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &g_expander), TAG, "expander device");
    }
    // Establish dark backlight and deselected LCD before enabling outputs.
    // The board's SPI CS46 remains separate from the expander's LCD enable.
    ESP_RETURN_ON_ERROR(set_output(kBacklight | kReset | kSelect, kReset | kSelect), TAG, "LCD latch");
    ESP_RETURN_ON_ERROR(update_i2c_register_bits(g_expander, 0x03, kBacklight | kReset | kSelect, 0, kTimeoutMs, TAG), TAG, "LCD direction");
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_RETURN_ON_ERROR(set_output(kReset, 0), TAG, "LCD reset low");
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_RETURN_ON_ERROR(set_output(kReset, kReset), TAG, "LCD reset release");
    ESP_RETURN_ON_ERROR(set_output(kSelect, 0), TAG, "LCD enable");
    if (!g_adc) {
        adc_oneshot_unit_init_cfg_t unit = {};
        unit.unit_id = ADC_UNIT_1;
        ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &g_adc), TAG, "key ADC1");
    }
    adc_oneshot_chan_cfg_t channel = {};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_12;
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(g_adc, ADC_CHANNEL_4, &channel), TAG, "key GPIO5 channel");
    if (!g_calibration) {
        adc_cali_curve_fitting_config_t calibration = {};
        calibration.unit_id = ADC_UNIT_1;
        calibration.chan = ADC_CHANNEL_4;
        calibration.atten = ADC_ATTEN_DB_12;
        calibration.bitwidth = ADC_BITWIDTH_12;
        // Button windows are millivolts; never compare them to raw ADC counts.
        ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&calibration, &g_calibration), TAG, "key ADC calibration");
    }
    g_ready = true;
    ESP_LOGI(TAG, "ready LCD_P1_backlight=P2_reset=P3_enable ADC1_CH4=mV PA48=off");
    return ESP_OK;
}

esp_err_t korvo2_set_backlight(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(kTimeoutMs)) != pdTRUE) return ESP_ERR_TIMEOUT;
    // Expander backlight supports on/off only, like the official BSP.
    const esp_err_t result = set_output(kBacklight, percent ? kBacklight : 0);
    xSemaphoreGive(g_lock);
    return result;
}

ManualInputKey korvo2_read_keys() {
    if (!g_ready) return ManualInputKey::Invalid;
    int raw = 0, millivolts = 0;
    esp_err_t result = adc_oneshot_read(g_adc, ADC_CHANNEL_4, &raw);
    if (result == ESP_OK) result = adc_cali_raw_to_voltage(g_calibration, raw, &millivolts);
    if (result != ESP_OK) {
        if (result != g_last_key_error) ESP_LOGE(TAG, "key_read failed: %s", esp_err_to_name(result));
        g_last_key_error = result;
        g_wait_for_release = true;
        return ManualInputKey::Invalid;
    }
    if (g_last_key_error != ESP_OK) ESP_LOGI(TAG, "key_read recovered; wait for release");
    g_last_key_error = ESP_OK;
    // Above the highest published key window is the release region. Actual
    // ladder voltages and simultaneous-key ambiguity still need measurement.
    if (millivolts > 2510) {
        g_wait_for_release = false;
        g_previous_key = ManualInputKey::None;
        return ManualInputKey::None;
    }
    ManualInputKey key = ManualInputKey::Invalid;
    if (millivolts >= 2310) key = ManualInputKey::Listen;
    else if (millivolts >= 280 && millivolts <= 480) key = ManualInputKey::VolumeUp;
    else if (millivolts >= 720 && millivolts <= 920) key = ManualInputKey::VolumeDown;
    // MUTE/PLAY/SET and gaps cancel; no accidental card confirmation or
    // reconfiguration gesture is assigned to those physical keys.
    if (key == ManualInputKey::Invalid || (g_previous_key != ManualInputKey::None && key != g_previous_key)) {
        g_wait_for_release = true;
    }
    if (g_wait_for_release) return ManualInputKey::Invalid;
    g_previous_key = key;
    return key;
}
