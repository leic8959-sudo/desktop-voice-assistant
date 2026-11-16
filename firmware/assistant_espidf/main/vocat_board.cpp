// Revision detection adapted from78/xiaozhi-esp32@49b4380b3faa0ceadd28a3e78aaf0f311c46c57e.
// Copyright (c)2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c)2025 Project Contributors. SPDX-License-Identifier: MIT
// Modified:checked errors/no revision fallback/RTC complement fields/noNVS writes.
// Original terms:third_party/xiaozhi_vocat/LICENSE.
#include "board_profile.h"

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "vocat_board";
constexpr uint32_t kRevisionMagic = 0x564f4301;
enum class Revision : uint32_t { Unknown = 0, V10 = 1, V12 = 2 };
struct RevisionCache { uint32_t magic, inverse_magic, revision, inverse_revision; };
RTC_NOINIT_ATTR volatile RevisionCache g_cache;
Revision g_revision = Revision::Unknown;
gpio_num_t g_amplifier = GPIO_NUM_NC;
SemaphoreHandle_t g_lock;
bool g_ready, g_started, g_amplifier_configured;

bool cache_valid() {
    return g_cache.magic == kRevisionMagic && g_cache.inverse_magic == ~kRevisionMagic &&
        (g_cache.revision == 1 || g_cache.revision == 2) && g_cache.inverse_revision == ~g_cache.revision;
}

esp_err_t set_codec_rail(bool enabled) {
    // Latch first:reconfiguringGPIO48 low during warmV1.2 startup cuts its
    // codec supply and can clamp the shared touch/fuel-gauge I2C bus.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_48, enabled ? 1 : 0), TAG, "codec rail latch48");
    gpio_config_t rail = {};
    rail.pin_bit_mask = 1ULL << GPIO_NUM_48;
    rail.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&rail);
}

esp_err_t probe_codecs(i2c_master_bus_handle_t bus, bool *dac_present, bool *adc_present) {
    const esp_err_t dac = i2c_master_probe(bus, 0x18, 100);
    if (dac != ESP_OK && dac != ESP_ERR_NOT_FOUND) return dac;
    // codec_dev's API80 is the physical7-bit ES7210 address40. The official
    // V1.2BSP gates both speaker and microphone supplies withGPIO48.
    const esp_err_t adc = i2c_master_probe(bus, 0x40, 100);
    if (adc != ESP_OK && adc != ESP_ERR_NOT_FOUND) return adc;
    *dac_present = dac == ESP_OK;
    *adc_present = adc == ESP_OK;
    ESP_LOGD(TAG, "codec presence DAC18=%d ADC40=%d", *dac_present, *adc_present);
    return ESP_OK;
}

esp_err_t wait_for_codecs(i2c_master_bus_handle_t bus) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(200));
        bool dac_present = false, adc_present = false;
        const esp_err_t result = probe_codecs(bus, &dac_present, &adc_present);
        ESP_RETURN_ON_ERROR(result, TAG, "codec pair probe");
        if (dac_present && adc_present) return ESP_OK;
    }
    ESP_LOGE(TAG, "codec pair did not become ready after rail startup");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t set_amplifier(bool enabled) {
    ESP_RETURN_ON_ERROR(gpio_set_level(g_amplifier, enabled ? 1 : 0), TAG, "selected PA latch");
    vTaskDelay(pdMS_TO_TICKS(1));
    if (gpio_get_level(g_amplifier) != (enabled ? 1 : 0)) {
        ESP_LOGE(TAG, "PA input mismatch pin=%d enabled=%d", g_amplifier, enabled);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t configure_selected_pins() {
    // GPIO15 is MIC DIN onV1.0,but PA onV1.2. Configure only the identified
    // amplifier candidate;never park both candidates as outputs.
    if (g_revision != Revision::V10 && g_revision != Revision::V12) return ESP_ERR_INVALID_STATE;
    g_amplifier = g_revision == Revision::V12 ? GPIO_NUM_15 : GPIO_NUM_4;
    spi_board::audio_input = g_revision == Revision::V12 ? GPIO_NUM_3 : GPIO_NUM_15;
    ESP_RETURN_ON_ERROR(gpio_set_level(g_amplifier, 0), TAG, "selected PA off latch");
    gpio_config_t amplifier = {};
    amplifier.pin_bit_mask = 1ULL << g_amplifier;
    amplifier.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&amplifier), TAG, "selected PA output/input readback");
    g_amplifier_configured = true;
    return set_amplifier(false);
}

esp_err_t initialize_board(i2c_master_bus_handle_t bus) {
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_44, 0), TAG, "backlight dark latch44");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_44;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight dark output");
    const esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_POWERON) {
        if (!cache_valid()) {
            // An OTA/flash/first software reset may have no cache from this
            // implementation. Raising48 is safe on both known revisions;
            // choosingV1.0from an ACK with residual rail charge is unsafe.
            ESP_RETURN_ON_ERROR(set_codec_rail(true), TAG, "unknown warm reset rail held on");
            ESP_LOGE(TAG, "unknown PCB revision on warm reset(%d);power-cycle board before retry;PA candidates untouched", reason);
            return ESP_ERR_INVALID_STATE;
        }
        g_revision = static_cast<Revision>(g_cache.revision);
        ESP_RETURN_ON_ERROR(set_codec_rail(g_revision == Revision::V12), TAG, "cached codec rail");
        ESP_RETURN_ON_ERROR(configure_selected_pins(), TAG, "cached revision pins/PA off");
        if (g_revision == Revision::V12) {
            ESP_RETURN_ON_ERROR(wait_for_codecs(bus), TAG, "cachedV1.2 codec presence");
        } else {
            bool dac_present = false, adc_present = false;
            ESP_RETURN_ON_ERROR(probe_codecs(bus, &dac_present, &adc_present), TAG, "cachedV1.0 codec probe");
            if (!dac_present || !adc_present) return ESP_ERR_NOT_FOUND;
        }
    } else {
        g_cache.magic = 0;
        g_cache.inverse_magic = 0;
        ESP_RETURN_ON_ERROR(set_codec_rail(false), TAG, "cold revision probe rail low");
        vTaskDelay(pdMS_TO_TICKS(100));
        bool dac_present = false, adc_present = false;
        esp_err_t probe = probe_codecs(bus, &dac_present, &adc_present);
        if (probe == ESP_OK && dac_present != adc_present) probe = ESP_ERR_INVALID_RESPONSE;
        if (probe == ESP_OK && dac_present && adc_present) {
            g_revision = Revision::V10;
        } else if (probe == ESP_OK && !dac_present && !adc_present) {
            ESP_RETURN_ON_ERROR(set_codec_rail(true), TAG, "coldV1.2 rail startup");
            probe = wait_for_codecs(bus);
            ESP_RETURN_ON_ERROR(probe, TAG, "coldV1.2 codec presence");
            g_revision = Revision::V12;
        } else {
            // A timeout is not proof of a board revision. Keep the possible
            // gated supply on,but stop without assigning DIN/PA/reset pins.
            const esp_err_t rescue = set_codec_rail(true);
            ESP_LOGE(TAG, "cold revision probe failed:error=%s rail_rescue=%s", esp_err_to_name(probe), esp_err_to_name(rescue));
            return probe;
        }
        ESP_RETURN_ON_ERROR(configure_selected_pins(), TAG, "cold revision pins/PA off");
    }
    // The owning application and manufacturer both hold GPIO9 low. It also
    // enables the SD rail;this entry never initializes SD or changes charging.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_9, 0), TAG, "LCD/sharedSD enable latch9");
    gpio_config_t display_enable = {};
    display_enable.pin_bit_mask = 1ULL << GPIO_NUM_9;
    display_enable.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&display_enable), TAG, "LCD/sharedSD enable output");
    g_cache.revision = static_cast<uint32_t>(g_revision);
    g_cache.inverse_revision = ~static_cast<uint32_t>(g_revision);
    g_cache.inverse_magic = ~kRevisionMagic;
    g_cache.magic = kRevisionMagic;
    g_ready = true;
    ESP_LOGI(TAG, "PCB V%s ready;DIN=%d PA=%d LCDRST=%d reset_active_high=%d rail48=%d reset_reason=%d;RTC cache only/noNVS writes",
        g_revision == Revision::V12 ? "1.2" : "1.0", spi_board::audio_input, g_amplifier,
        vocat_lcd_reset_pin(), vocat_lcd_reset_active_high(), g_revision == Revision::V12, reason);
    return ESP_OK;
}
}

esp_err_t vocat_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus || g_started) return ESP_ERR_INVALID_STATE;
    g_started = true;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    const esp_err_t result = initialize_board(bus);
    if (result != ESP_OK) {
        if (g_amplifier_configured) {
            const esp_err_t cut = set_amplifier(false);
            ESP_LOGE(TAG, "board startup failed:error=%s selected_PA_cut=%s", esp_err_to_name(result), esp_err_to_name(cut));
        } else ESP_LOGE(TAG, "board startup failed:error=%s;noPA candidate configured", esp_err_to_name(result));
    }
    return result;
}
bool vocat_board_ready() { return g_ready; }
gpio_num_t vocat_lcd_reset_pin() { return g_ready ? (g_revision == Revision::V12 ? GPIO_NUM_47 : GPIO_NUM_3) : GPIO_NUM_NC; }
bool vocat_lcd_reset_active_high() { return g_ready && g_revision == Revision::V12; }

esp_err_t vocat_set_amplifier(bool enabled) {
    if (!g_amplifier_configured || !g_lock) return ESP_ERR_INVALID_STATE;
    if (enabled && !g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = set_amplifier(enabled);
    xSemaphoreGive(g_lock);
    return result;
}
