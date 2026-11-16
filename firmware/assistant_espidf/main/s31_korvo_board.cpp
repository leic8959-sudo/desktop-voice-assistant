#include "s31_korvo_board.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "third_party/s31_korvo/bsp_adc_calibration.h"

namespace {
constexpr const char *TAG = "s31_korvo_board";
i2c_master_bus_handle_t g_i2c;
adc_oneshot_unit_handle_t g_adc;
bool g_attempted, g_ready, g_wait_for_release = true, g_key_error;
ManualInputKey g_candidate = ManualInputKey::Invalid;
ManualInputKey g_active = ManualInputKey::None;
unsigned g_stable_count;

esp_err_t initialize() {
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_7, 0), TAG, "PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << GPIO_NUM_7; amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), TAG, "PA pad with readback");
    gpio_config_t boot = {};
    boot.pin_bit_mask = 1ULL << GPIO_NUM_61; boot.mode = GPIO_MODE_INPUT; boot.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&boot), TAG, "BOOT61 input");
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0; bus.sda_io_num = GPIO_NUM_0; bus.scl_io_num = GPIO_NUM_1;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_i2c), TAG, "codec/touch I2C0");
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &g_adc), TAG, "key ADC1");
    adc_oneshot_chan_cfg_t channel = {};
    channel.atten = ADC_ATTEN_DB_0; channel.bitwidth = ADC_BITWIDTH_DEFAULT;
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(g_adc, ADC_CHANNEL_0, &channel), TAG, "key GPIO42/ADC1 channel0");
    // S31 raw values run opposite to voltage. Use the pinned BSP software
    // calibration instead of applying a generic raw-to-mV linear formula.
    return bsp_s31_adc_calibration_init(g_adc, ADC_UNIT_1, ADC_CHANNEL_0);
}
}

esp_err_t s31_korvo_board_init() {
    if (g_attempted) return g_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_attempted = true;
    const esp_err_t result = initialize();
    g_ready = result == ESP_OK;
    if (!g_ready) {
        const esp_err_t mute = gpio_set_level(GPIO_NUM_7, 0);
        ESP_LOGE(TAG, "startup stopped:%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(mute));
    }
    return result;
}

ManualInputKey s31_korvo_read_keys() {
    if (!g_ready) return ManualInputKey::Invalid;
    uint32_t total = 0;
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < 4 && result == ESP_OK; ++i) {
        int raw = 0;
        result = adc_oneshot_read(g_adc, ADC_CHANNEL_0, &raw);
        if (result == ESP_OK && (raw < 0 || raw > 0x1FFFF)) result = ESP_ERR_INVALID_RESPONSE;
        if (result == ESP_OK) total += raw;
    }
    int mv = 0;
    if (result == ESP_OK) result = bsp_s31_adc_calibration_raw_to_mv(ADC_UNIT_1, static_cast<int>(total / 4), &mv);
    if (result != ESP_OK) {
        if (!g_key_error) ESP_LOGE(TAG, "key ADC failed:%s", esp_err_to_name(result));
        g_key_error = true; g_wait_for_release = true; g_stable_count = 0; g_active = ManualInputKey::None;
        return ManualInputKey::Invalid;
    }
    if (g_key_error) ESP_LOGI(TAG, "key ADC recovered;waiting for release");
    g_key_error = false;
    // Centers380/820/1340/1870 and idle2000mV, from the owning board.
    // Exact window boundaries are ambiguous, so they cancel rather than
    // belonging to two buttons. SET remains reserved for future safe reconfiguration.
    ManualInputKey key = ManualInputKey::Invalid;
    if (mv > 1935 && mv <= 2000) key = ManualInputKey::None;
    else if (mv >= 160 && mv < 600) key = ManualInputKey::VolumeUp;
    else if (mv > 600 && mv < 1080) key = ManualInputKey::VolumeDown;
    else if (mv > 1080 && mv < 1605) key = ManualInputKey::Listen; // MODE
    const bool boot = !gpio_get_level(GPIO_NUM_61);
    if (boot) key = key == ManualInputKey::None ? ManualInputKey::Listen : ManualInputKey::Invalid;
    if (key != g_candidate) { g_candidate = key; g_stable_count = 1; }
    else if (g_stable_count < 3) ++g_stable_count;
    if (key == ManualInputKey::Invalid) {
        g_wait_for_release = true; g_active = ManualInputKey::None;
        return key;
    }
    if (g_wait_for_release) {
        if (key == ManualInputKey::None && g_stable_count >= 3) {
            g_wait_for_release = false; g_active = ManualInputKey::None;
            return ManualInputKey::None;
        }
        return ManualInputKey::Invalid;
    }
    // A change to another held key never becomes a fresh action without release.
    if (g_active != ManualInputKey::None && key != ManualInputKey::None && key != g_active) {
        g_wait_for_release = true; g_active = ManualInputKey::None;
        return ManualInputKey::Invalid;
    }
    if (g_stable_count >= 3) g_active = key;
    return g_active;
}

esp_err_t bsp_i2c_init() { return s31_korvo_board_init(); }
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }
