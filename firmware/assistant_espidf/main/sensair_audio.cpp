#include "sensair_audio.h"
#include "sensair_board.h"
#include "sensair_adc_owner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include "audio_codec_if.h"
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/gpio_sig_map.h"

namespace {
constexpr const char *kTag = "sensair_audio";
constexpr gpio_num_t kPa = GPIO_NUM_1;
SemaphoreHandle_t g_lock;
i2s_chan_handle_t g_tx;
esp_codec_dev_handle_t g_speaker, g_microphone;
const audio_codec_data_if_t *g_adc;
audio_codec_data_if_t g_data{};
audio_codec_if_t g_codec{};
std::atomic<bool> g_ready{false};
std::atomic<esp_err_t> g_error{ESP_OK};
bool g_attempted, g_formatted, g_enabled, g_codec_enabled, g_explicit_unmute, g_muted = true;

bool lock() { return g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) == pdTRUE; }
void unlock() { xSemaphoreGive(g_lock); }
void fault(esp_err_t error, const char *stage) {
    if (error == ESP_OK) return;
    esp_err_t expected = ESP_OK;
    if (g_error.compare_exchange_strong(expected, error)) ESP_LOGE(kTag, "%s failed=%s; restart required", stage, esp_err_to_name(error));
}
esp_err_t amplifier(bool enabled) {
    const esp_err_t written = gpio_set_level(kPa, enabled ? 1 : 0);
    const esp_err_t result = written == ESP_OK && gpio_get_level(kPa) != (enabled ? 1 : 0) ? ESP_ERR_INVALID_RESPONSE : written;
    fault(result, "PA1 write/readback");
    return result;
}
bool valid_format(const esp_codec_dev_sample_info_t *format) {
    return format && format->sample_rate == 24000 && format->bits_per_sample == 16 && format->channel == 1;
}
bool data_open(const audio_codec_data_if_t *) { return g_ready.load(); }
bool codec_open(const audio_codec_if_t *) { return g_ready.load(); }

int format(const audio_codec_data_if_t *, esp_codec_dev_type_t type, esp_codec_dev_sample_info_t *format) {
    if (type != ESP_CODEC_DEV_TYPE_OUT || !valid_format(format)) return ESP_CODEC_DEV_INVALID_ARG;
    if (!lock()) return ESP_CODEC_DEV_DRV_ERR;
    if (g_enabled || g_error.load() != ESP_OK) { unlock(); return ESP_CODEC_DEV_WRONG_STATE; }
    i2s_pdm_tx_clk_config_t clock = I2S_PDM_TX_CLK_DEFAULT_CONFIG(24000);
    clock.up_sample_fs = 480;
    i2s_pdm_tx_slot_config_t slot = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    slot.sd_scale = I2S_PDM_SIG_SCALING_MUL_4; slot.hp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    slot.lp_scale = I2S_PDM_SIG_SCALING_MUL_4; slot.sinc_scale = I2S_PDM_SIG_SCALING_MUL_4;
    esp_err_t result = i2s_channel_reconfig_pdm_tx_clock(g_tx, &clock);
    if (result == ESP_OK) result = i2s_channel_reconfig_pdm_tx_slot(g_tx, &slot);
    g_formatted = result == ESP_OK;
    fault(result, "fixed PDM format");
    unlock();
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
int data_enable(const audio_codec_data_if_t *, esp_codec_dev_type_t type, bool enabled) {
    if (type != ESP_CODEC_DEV_TYPE_OUT || !lock()) return ESP_CODEC_DEV_WRONG_STATE;
    if (enabled && (!g_formatted || g_error.load() != ESP_OK)) { unlock(); return ESP_CODEC_DEV_WRONG_STATE; }
    esp_err_t result = ESP_OK;
    if (!enabled) { g_explicit_unmute = false; g_muted = true; result = amplifier(false); }
    if (g_enabled != enabled) {
        const esp_err_t changed = enabled ? i2s_channel_enable(g_tx) : i2s_channel_disable(g_tx);
        if (changed == ESP_OK) g_enabled = enabled;
        if (result == ESP_OK) result = changed;
        fault(changed, "PDM channel enable");
    }
    unlock();
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
int codec_enable(const audio_codec_if_t *, bool enabled) {
    if (!lock()) return ESP_CODEC_DEV_DRV_ERR;
    esp_err_t result = ESP_OK;
    if (!enabled) { g_explicit_unmute = false; g_muted = true; result = amplifier(false); }
    else if (!g_enabled || !g_formatted || g_error.load() != ESP_OK) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) g_codec_enabled = enabled;
    unlock();
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
int codec_format(const audio_codec_if_t *, esp_codec_dev_sample_info_t *format) {
    return valid_format(format) && g_error.load() == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_INVALID_ARG;
}
int mute(const audio_codec_if_t *, bool muted) {
    if (!lock()) return ESP_CODEC_DEV_DRV_ERR;
    g_muted = true;
    esp_err_t result;
    if (muted || !g_explicit_unmute) result = amplifier(false);
    else if (g_enabled && g_codec_enabled && g_formatted && g_error.load() == ESP_OK) {
        result = amplifier(true);
        if (result == ESP_OK) g_muted = false;
        else (void)amplifier(false);
    } else result = ESP_ERR_INVALID_STATE;
    unlock();
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
int write(const audio_codec_data_if_t *, uint8_t *data, int size) {
    if (!data || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    const int64_t deadline = esp_timer_get_time() + 1000000;
    if (!lock()) return ESP_CODEC_DEV_DRV_ERR;
    if (!g_enabled || !g_codec_enabled || g_error.load() != ESP_OK) { unlock(); return ESP_CODEC_DEV_WRONG_STATE; }
    std::array<uint8_t, 1024> silence{};
    size_t completed = 0;
    esp_err_t result = ESP_OK;
    while (completed < static_cast<size_t>(size)) {
        const int64_t left = deadline - esp_timer_get_time();
        if (left <= 0) { result = ESP_ERR_TIMEOUT; break; }
        const size_t count = std::min(static_cast<size_t>(size) - completed, silence.size());
        size_t sent = 0;
        result = i2s_channel_write(g_tx, g_muted ? silence.data() : data + completed, count, &sent, static_cast<uint32_t>((left + 999) / 1000));
        completed += sent;
        if (result != ESP_OK || !sent) { if (result == ESP_OK) result = ESP_ERR_TIMEOUT; break; }
    }
    if (result != ESP_OK) { g_muted = true; fault(result, "PDM write"); (void)amplifier(false); }
    unlock();
    return result == ESP_OK && completed == static_cast<size_t>(size) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

esp_err_t create_output() {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    esp_err_t result = i2s_new_channel(&channel, &g_tx, nullptr);
    if (result != ESP_OK) return result;
    i2s_pdm_tx_config_t pdm = {};
    pdm.clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(24000); pdm.clk_cfg.up_sample_fs = 480;
    pdm.slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    pdm.slot_cfg.sd_scale = I2S_PDM_SIG_SCALING_MUL_4; pdm.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm.slot_cfg.lp_scale = I2S_PDM_SIG_SCALING_MUL_4; pdm.slot_cfg.sinc_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm.gpio_cfg.clk = GPIO_NUM_NC; pdm.gpio_cfg.dout = GPIO_NUM_7;
    // C5 has two PDM lanes; this board uses one lane plus a matrix-inverted
    // copy, so keep the unused native second lane disconnected explicitly.
    pdm.gpio_cfg.dout2 = GPIO_NUM_NC;
    result = i2s_channel_init_pdm_tx_mode(g_tx, &pdm);
    if (result != ESP_OK) return result;
    gpio_config_t negative = {};
    negative.pin_bit_mask = 1ULL << GPIO_NUM_8; negative.mode = GPIO_MODE_OUTPUT;
    result = gpio_config(&negative);
    if (result != ESP_OK) return result;
    esp_rom_gpio_connect_out_signal(GPIO_NUM_8, I2SO_SD_OUT_IDX, true, false);
    result = gpio_set_drive_capability(GPIO_NUM_7, GPIO_DRIVE_CAP_0);
    if (result == ESP_OK) result = gpio_set_drive_capability(GPIO_NUM_8, GPIO_DRIVE_CAP_0);
    return result;
}
}

esp_err_t sensair_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone) {
    if (!speaker || !microphone || g_attempted) return ESP_ERR_INVALID_STATE;
    g_attempted = true; *speaker = nullptr; *microphone = nullptr;
    esp_err_t result = sensair_board_init();
    if (result != ESP_OK) return result;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    g_adc = sensair_adc_owner_new();
    if (!g_adc) return ESP_FAIL;
    esp_codec_dev_cfg_t input = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = nullptr, .data_if = g_adc};
    g_microphone = esp_codec_dev_new(&input);
    result = g_microphone ? create_output() : ESP_ERR_NO_MEM;
    if (result == ESP_OK) {
        g_data.is_open = data_open; g_data.set_fmt = format; g_data.enable = data_enable; g_data.write = write;
        g_codec.is_open = codec_open; g_codec.enable = codec_enable; g_codec.set_fs = codec_format; g_codec.mute = mute;
        // No set_vol callback: codec_dev uses its existing PCM16 software
        // volume processor, whose allocation failure surfaces via set_out_vol.
        esp_codec_dev_cfg_t output = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = &g_codec, .data_if = &g_data};
        g_speaker = esp_codec_dev_new(&output);
        if (!g_speaker) result = ESP_ERR_NO_MEM;
    }
    if (result != ESP_OK) {
        if (g_speaker) esp_codec_dev_delete(g_speaker);
        if (g_microphone) esp_codec_dev_delete(g_microphone);
        const int adc_closed = audio_codec_delete_data_if(g_adc);
        if (adc_closed != ESP_CODEC_DEV_OK) ESP_LOGE(kTag, "ADC cleanup failed=%d; restart required", adc_closed);
        if (g_tx) { const esp_err_t deleted = i2s_del_channel(g_tx); if (deleted != ESP_OK) ESP_LOGE(kTag, "PDM cleanup failed=%s", esp_err_to_name(deleted)); }
        return result;
    }
    g_ready = true;
    *speaker = g_speaker; *microphone = g_microphone;
    return ESP_OK;
}
esp_err_t sensair_audio_status() {
    if (!g_ready.load() || !lock()) return ESP_FAIL;
    const bool output = g_error.load() == ESP_OK && g_formatted && g_enabled && g_codec_enabled;
    unlock();
    return output && sensair_adc_owner_ready(g_adc) ? ESP_OK : ESP_FAIL;
}
int sensair_audio_set_mute(bool muted) {
    if (!g_ready.load()) return ESP_CODEC_DEV_WRONG_STATE;
    if (!lock()) return ESP_CODEC_DEV_DRV_ERR;
    g_explicit_unmute = !muted;
    unlock();
    const int library = esp_codec_dev_set_out_mute(g_speaker, muted);
    const int actual = mute(&g_codec, muted); // Check callback result discarded by codec_dev.
    return library == ESP_CODEC_DEV_OK && actual == ESP_CODEC_DEV_OK && g_error.load() == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}
