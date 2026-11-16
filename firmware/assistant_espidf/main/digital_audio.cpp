#include "digital_audio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include "board_profile.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "dummy_codec.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr const char *TAG = "digital_audio";
constexpr uint32_t kTransferTimeoutMs = 1000;
struct DigitalData {
    audio_codec_data_if_t interface{};
    i2s_chan_handle_t channel = nullptr;
    bool input = false;
    bool enabled = false;
    esp_err_t error = ESP_OK;
};
DigitalData g_input, g_output;
const audio_codec_gpio_if_t *g_gpio;
const audio_codec_if_t *g_amplifier;
#if defined(ASSISTANT_BOARD_K10)
const audio_codec_ctrl_if_t *g_input_control;
const audio_codec_if_t *g_input_codec;
#endif
audio_codec_if_t g_output_interface{};
std::atomic<bool> g_muted{true};

#if defined(ASSISTANT_BOARD_MVSRLORA)
esp_err_t g_amplifier_error = ESP_OK;
#endif
bool amplifier_open(const audio_codec_if_t *) { return g_amplifier->is_open(g_amplifier); }
int amplifier_enable(const audio_codec_if_t *, bool enabled) {
#if defined(ASSISTANT_BOARD_MVSRLORA)
    if (!enabled) {
        g_muted.store(true);
        g_amplifier_error = mvsrlora_set_amplifier(false);
        if (g_amplifier_error != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
    }
#endif
    return g_amplifier->enable(g_amplifier, enabled);
}
int amplifier_close(const audio_codec_if_t *) {
    g_muted.store(true);
#if defined(ASSISTANT_BOARD_MVSRLORA)
    g_amplifier_error = mvsrlora_set_amplifier(false);
    if (g_amplifier_error != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
#endif
    return g_amplifier->close(g_amplifier);
}
int amplifier_mute(const audio_codec_if_t *, bool muted) {
#if defined(ASSISTANT_BOARD_MVSRLORA)
    if (muted) {
        g_muted.store(true);
        g_amplifier_error = mvsrlora_set_amplifier(false);
        if (g_amplifier_error != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
    }
#endif
    const int result = g_amplifier->mute(g_amplifier, muted);
#if defined(ASSISTANT_BOARD_MVSRLORA)
    if (result == ESP_CODEC_DEV_OK && !muted) {
        g_amplifier_error = mvsrlora_set_amplifier(true);
        if (g_amplifier_error != ESP_OK) {
            g_muted.store(true);
            const esp_err_t cut = mvsrlora_set_amplifier(false);
            ESP_LOGE(TAG, "PA enable failed:%s recovery_cut=%s", esp_err_to_name(g_amplifier_error), esp_err_to_name(cut));
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
#endif
    if (result == ESP_CODEC_DEV_OK) g_muted.store(muted);
    return result;
}

DigitalData *data_state(const audio_codec_data_if_t *interface) {
    return reinterpret_cast<DigitalData *>(const_cast<audio_codec_data_if_t *>(interface));
}

bool data_is_open(const audio_codec_data_if_t *interface) { return data_state(interface)->channel != nullptr; }

int data_enable(const audio_codec_data_if_t *interface, esp_codec_dev_type_t, bool enabled) {
    auto *state = data_state(interface);
    if (enabled && state->error != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
#if defined(ASSISTANT_BOARD_K10)
    // The ADC and speaker shareI2S0BCLK/WS. StartTXfirst for stable shared
    // clocks before RX;the normalclose order stopsRX beforeTX.
    if (state->input && enabled && !g_output.enabled) {
        const int output = data_enable(&g_output.interface, ESP_CODEC_DEV_TYPE_OUT, true);
        if (output != ESP_CODEC_DEV_OK) return output;
    }
#endif
    if (state->enabled == enabled) return ESP_CODEC_DEV_OK;
    const esp_err_t result = enabled ? i2s_channel_enable(state->channel) : i2s_channel_disable(state->channel);
    if (result != ESP_OK) {
        state->error = result;
        ESP_LOGE(TAG, "channel_enable input=%d enabled=%d error=%s", state->input, enabled, esp_err_to_name(result));
        return ESP_CODEC_DEV_DRV_ERR;
    }
    state->enabled = enabled;
    return ESP_CODEC_DEV_OK;
}

int data_format(const audio_codec_data_if_t *interface, esp_codec_dev_type_t, esp_codec_dev_sample_info_t *format) {
    auto *state = data_state(interface);
    const uint32_t expected_rate = state->input ? board_profile::microphone_sample_rate : board_profile::speaker_sample_rate;
    // Fixed hardware clocks: esp_codec_dev must not silently turn PDM32 into I2S24.
    state->error = format && format->sample_rate == expected_rate && format->channel == 1 &&
        format->bits_per_sample == 16 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
    return state->error == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_NOT_SUPPORT;
}

int transfer(const audio_codec_data_if_t *interface, uint8_t *buffer, int size, bool input) {
    auto *state = data_state(interface);
    if (!buffer || size <= 0 || size % sizeof(int16_t) || input != state->input) return ESP_CODEC_DEV_INVALID_ARG;
    if (!state->enabled || state->error != ESP_OK) return ESP_CODEC_DEV_WRONG_STATE;
    // Partial transfers are accumulated under one deadline. Never return success
    // with unfilled capture samples or an unwritten tail of the speaker buffer.
    const int64_t deadline = esp_timer_get_time() + kTransferTimeoutMs * 1000LL;
    size_t completed = 0;
    esp_err_t result = ESP_OK;
    while (completed < static_cast<size_t>(size)) {
        const int64_t remaining_us = deadline - esp_timer_get_time();
        if (remaining_us <= 0) { result = ESP_ERR_TIMEOUT; break; }
        size_t transferred = 0;
        const uint32_t timeout = static_cast<uint32_t>((remaining_us + 999) / 1000);
        result = input ? i2s_channel_read(state->channel, buffer + completed, size - completed, &transferred, timeout)
            : i2s_channel_write(state->channel, buffer + completed, size - completed, &transferred, timeout);
        completed += transferred;
        if (result != ESP_OK) break;
        if (transferred == 0) { result = ESP_ERR_TIMEOUT; break; }
    }
    if (result != ESP_OK || completed != static_cast<size_t>(size)) {
        if (input) std::memset(buffer, 0, size);
        ESP_LOGE(TAG, "transfer input=%d bytes=%u/%d error=%s", input, static_cast<unsigned>(completed), size, esp_err_to_name(result));
        return ESP_CODEC_DEV_DRV_ERR;
    }
    return ESP_CODEC_DEV_OK;
}

int data_read(const audio_codec_data_if_t *interface, uint8_t *buffer, int size) {
#if defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_K10)
    if (!buffer || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    std::array<int16_t, 1024> stereo{};
    const size_t samples = static_cast<size_t>(size) / sizeof(int16_t);
    if (samples > stereo.size() / 2) return ESP_CODEC_DEV_INVALID_ARG;
    const int result = transfer(interface, reinterpret_cast<uint8_t *>(stereo.data()), samples * 2 * sizeof(int16_t), true);
    if (result != ESP_CODEC_DEV_OK) { std::memset(buffer, 0, size); return result; }
    auto *pcm = reinterpret_cast<int16_t *>(buffer);
    for (size_t index = 0; index < samples; ++index) pcm[index] = stereo[index * 2];
    return ESP_CODEC_DEV_OK;
#elif defined(ASSISTANT_BOARD_TCAMERA)
    if (!buffer || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
#if defined(ASSISTANT_BOARD_TCAMERA_V12)
    const int result = transfer(interface, buffer, size, true);
    if (result != ESP_CODEC_DEV_OK) return result;
    auto *pcm = reinterpret_cast<int16_t *>(buffer);
    for (size_t index = 0; index < static_cast<size_t>(size) / sizeof(int16_t); ++index) {
        const int32_t value = static_cast<int32_t>(pcm[index]) * 20;
        pcm[index] = static_cast<int16_t>(std::clamp<int32_t>(value, -32768, 32767));
    }
#else
    // MSM261hardware is right-channel24-bit I2S. Receive full32-bit slots;
    // fixedsource's16-bit/ws-inverted shortcut loses the framing contract.
    std::array<int32_t, 512> slots{};
    const size_t samples = static_cast<size_t>(size) / sizeof(int16_t);
    if (samples > slots.size()) return ESP_CODEC_DEV_INVALID_ARG;
    const int result = transfer(interface, reinterpret_cast<uint8_t *>(slots.data()), samples * sizeof(int32_t), true);
    if (result != ESP_CODEC_DEV_OK) { std::memset(buffer, 0, size); return result; }
    auto *pcm = reinterpret_cast<int16_t *>(buffer);
    for (size_t index = 0; index < samples; ++index) {
        const int32_t value = (slots[index] >> 16) * 20;
        pcm[index] = static_cast<int16_t>(std::clamp<int32_t>(value, -32768, 32767));
    }
#endif
    // Preserve the owningapplication's20x software boost;notanalog gain.
    return ESP_CODEC_DEV_OK;
#elif defined(ASSISTANT_BOARD_TCIRCLE_V11) || defined(ASSISTANT_BOARD_TAIJI_PDM) || defined(ASSISTANT_BOARD_DOIT) || defined(ASSISTANT_BOARD_DF_AI_CAM) || defined(ASSISTANT_BOARD_MVSRLORA)
    return transfer(interface, buffer, size, true);
#else
    if (!buffer || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    // Digital I2S microphones transmit signed data in32-bit slots. Read the full
    // slot and scale to PCM16 explicitly rather than rely on16-bit truncation.
    std::array<int32_t, 512> slots{};
    const size_t samples = static_cast<size_t>(size) / sizeof(int16_t);
    if (samples > slots.size()) return ESP_CODEC_DEV_INVALID_ARG;
    const int result = transfer(interface, reinterpret_cast<uint8_t *>(slots.data()), samples * sizeof(int32_t), true);
    if (result != ESP_CODEC_DEV_OK) { std::memset(buffer, 0, size); return result; }
    auto *pcm = reinterpret_cast<int16_t *>(buffer);
    for (size_t index = 0; index < samples; ++index) {
#if defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_TAIJI) || defined(ASSISTANT_BOARD_HEADLESS)
        // Match the fixed digital-mic frontend's four-bit PCM boost, saturating
        // before conversion. This is software scaling, not an analog gain API.
        const int32_t value = slots[index] >> 12;
        pcm[index] = static_cast<int16_t>(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
#else
        pcm[index] = static_cast<int16_t>(slots[index] >> 16);
#endif
    }
    return ESP_CODEC_DEV_OK;
#endif
}
int data_write(const audio_codec_data_if_t *interface, uint8_t *buffer, int size) {
    // PCM5101 boards have no PA GPIO. A dummy codec's successful mute alone
    // would leave their audio audible, so silence the actual transmitted PCM.
    if (buffer && size > 0 && g_muted.load()) std::memset(buffer, 0, size);
#if defined(ASSISTANT_BOARD_TCIRCLE) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_K10)
    if (!buffer || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    std::array<int16_t, 512> stereo{};
    const auto *pcm = reinterpret_cast<const int16_t *>(buffer);
    const size_t samples = static_cast<size_t>(size) / sizeof(int16_t);
    size_t completed = 0;
    while (completed < samples) {
        const size_t count = std::min(samples - completed, stereo.size() / 2);
        for (size_t index = 0; index < count; ++index) stereo[index * 2] = stereo[index * 2 + 1] = pcm[completed + index];
        const int result = transfer(interface, reinterpret_cast<uint8_t *>(stereo.data()), count * 2 * sizeof(int16_t), false);
        if (result != ESP_CODEC_DEV_OK) return result;
        completed += count;
    }
    return ESP_CODEC_DEV_OK;
#elif defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_TAIJI) || defined(ASSISTANT_BOARD_HEADLESS)
    if (!buffer || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    std::array<int32_t, 512> slots{};
    const auto *pcm = reinterpret_cast<const int16_t *>(buffer);
    const size_t samples = static_cast<size_t>(size) / sizeof(int16_t);
    size_t completed = 0;
    while (completed < samples) {
        const size_t count = samples - completed < slots.size() ? samples - completed : slots.size();
        for (size_t index = 0; index < count; ++index) slots[index] = static_cast<int32_t>(pcm[completed + index]) * 65536;
        const int result = transfer(interface, reinterpret_cast<uint8_t *>(slots.data()), count * sizeof(int32_t), false);
        if (result != ESP_CODEC_DEV_OK) return result;
        completed += count;
    }
    return ESP_CODEC_DEV_OK;
#else
    return transfer(interface, buffer, size, false);
#endif
}
int data_close(const audio_codec_data_if_t *interface) { return data_enable(interface, ESP_CODEC_DEV_TYPE_NONE, false); }

void setup_interface(DigitalData &state, bool input) {
    state.input = input;
    state.interface.is_open = data_is_open;
    state.interface.enable = data_enable;
    state.interface.set_fmt = data_format;
    state.interface.read = input ? data_read : nullptr;
    state.interface.write = input ? nullptr : data_write;
    state.interface.close = data_close;
}

void release_audio(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone) {
    if (*speaker) { esp_codec_dev_delete(*speaker); *speaker = nullptr; }
    if (*microphone) { esp_codec_dev_delete(*microphone); *microphone = nullptr; }
#if defined(ASSISTANT_BOARD_K10)
    if (g_input_codec) { audio_codec_delete_codec_if(g_input_codec); g_input_codec = nullptr; }
    if (g_input_control) { audio_codec_delete_ctrl_if(g_input_control); g_input_control = nullptr; }
#endif
    if (g_amplifier) { audio_codec_delete_codec_if(g_amplifier); g_amplifier = nullptr; }
    if (g_gpio) { audio_codec_delete_gpio_if(g_gpio); g_gpio = nullptr; }
    if (g_input.channel) { i2s_del_channel(g_input.channel); g_input.channel = nullptr; }
    if (g_output.channel) { i2s_del_channel(g_output.channel); g_output.channel = nullptr; }
}
}

#if defined(ASSISTANT_BOARD_MVSRLORA)
int digital_audio_set_output_mute(bool muted) {
    return g_amplifier ? amplifier_mute(&g_output_interface, muted) : ESP_CODEC_DEV_WRONG_STATE;
}
#endif

esp_err_t digital_audio_open_status() {
#if defined(ASSISTANT_BOARD_MVSRLORA)
    if (g_amplifier_error != ESP_OK) return g_amplifier_error;
#endif
    if (g_input.error != ESP_OK) return g_input.error;
    if (g_output.error != ESP_OK) return g_output.error;
    return g_input.enabled && g_output.enabled ? ESP_OK : ESP_ERR_INVALID_STATE;
}

#if defined(ASSISTANT_BOARD_K10)
int digital_audio_set_microphone_gain(float gain) {
    if (!g_input_codec || !g_input_codec->set_mic_gain) return ESP_CODEC_DEV_WRONG_STATE;
    if (gain != 37.5f) return ESP_CODEC_DEV_NOT_SUPPORT;
    // esp_codec_dev1.5.11discardsanalog gain callback errors. Check the actual
    // callback and bothPGAregisters;do not use driver's brokenget_reg pointer.
    const int result = g_input_codec->set_mic_gain(g_input_codec, gain);
    if (result != ESP_CODEC_DEV_OK) return result;
    for (int reg : {0x20, 0x21}) {
        uint8_t actual = 0;
        const int read = g_input_control->read_reg(g_input_control, reg, 1, &actual, 1);
        if (read != ESP_CODEC_DEV_OK || actual != 0x1e) {
            ESP_LOGE(TAG, "ES7243EPGAreadbackfailed reg=%02x result=%d actual=%02x", reg, read, actual);
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
    return ESP_CODEC_DEV_OK;
}
#endif

esp_err_t digital_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone) {
    if (!speaker || !microphone || g_input.channel || g_output.channel) return ESP_ERR_INVALID_STATE;
    *speaker = nullptr;
    *microphone = nullptr;
    setup_interface(g_input, true);
    setup_interface(g_output, false);
#if defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_K10)
    i2s_chan_config_t duplex_channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    duplex_channel.auto_clear = true;
    esp_err_t result = i2s_new_channel(&duplex_channel, &g_output.channel, &g_input.channel);
    i2s_std_config_t duplex = {};
    duplex.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(board_profile::speaker_sample_rate);
    duplex.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    duplex.gpio_cfg = {.mclk =
#if defined(ASSISTANT_BOARD_K10)
        GPIO_NUM_3,
#else
        I2S_GPIO_UNUSED,
#endif
        .bclk = digital_audio_pins::speaker_bclk, .ws = digital_audio_pins::speaker_word_select,
        .dout = digital_audio_pins::speaker_data, .din = digital_audio_pins::microphone_data,
        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}};
    if (result == ESP_OK) result = i2s_channel_init_std_mode(g_output.channel, &duplex);
    if (result == ESP_OK) result = i2s_channel_init_std_mode(g_input.channel, &duplex);
#else
    i2s_chan_config_t input_channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_config_t output_channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    output_channel.auto_clear = true;
    esp_err_t result = ESP_OK;
#if defined(ASSISTANT_BOARD_OTTO)
    if (otto_camera_board()) {
        // Camera hardware shares BCLK/WS and uses16kHz on both directions.
        input_channel.auto_clear = true;
        result = i2s_new_channel(&input_channel, &g_output.channel, &g_input.channel);
    } else
#endif
    {
        result = i2s_new_channel(&input_channel, nullptr, &g_input.channel);
        if (result == ESP_OK) result = i2s_new_channel(&output_channel, &g_output.channel, nullptr);
    }
#if defined(ASSISTANT_BOARD_TCIRCLE_V11) || defined(ASSISTANT_BOARD_TAIJI_PDM) || defined(ASSISTANT_BOARD_DOIT) || defined(ASSISTANT_BOARD_DF_AI_CAM) || defined(ASSISTANT_BOARD_TCAMERA_V12) || defined(ASSISTANT_BOARD_MVSRLORA)
    i2s_pdm_rx_config_t input = {};
    input.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(board_profile::microphone_sample_rate);
    input.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    input.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
    input.gpio_cfg.clk = digital_audio_pins::microphone_clock;
    input.gpio_cfg.din = digital_audio_pins::microphone_data;
    if (result == ESP_OK) result = i2s_channel_init_pdm_rx_mode(g_input.channel, &input);
#else
    i2s_std_config_t input = {};
    input.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(board_profile::microphone_sample_rate);
    input.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    input.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    input.slot_cfg.ws_width = 32;
#if defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_HU087) || defined(ASSISTANT_BOARD_TCAMERA)
    input.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#else
    input.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#endif
    input.gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = digital_audio_pins::microphone_bclk, .ws = digital_audio_pins::microphone_clock, .dout = I2S_GPIO_UNUSED, .din = digital_audio_pins::microphone_data,
        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}};
#if defined(ASSISTANT_BOARD_OTTO)
    if (otto_camera_board()) input.gpio_cfg.dout = digital_audio_pins::speaker_data;
#endif
    if (result == ESP_OK) result = i2s_channel_init_std_mode(g_input.channel, &input);
#endif
    i2s_std_config_t output = {};
    output.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(board_profile::speaker_sample_rate);
#if defined(ASSISTANT_BOARD_TCIRCLE) || defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA)
    // MAX98357datasheetp19explicitlyrejects24kHzand11025Hz. Stereo16at16kHz
    // with duplicatedmono serves the supported boards' SD_MODE channel selection.
    output.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#elif defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_OLED) || defined(ASSISTANT_BOARD_TAIJI) || defined(ASSISTANT_BOARD_HEADLESS)
    output.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
#if defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TAIJI)
    output.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
#elif defined(ASSISTANT_BOARD_HU087) || defined(ASSISTANT_BOARD_WK)
    output.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#else
    output.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#endif
#else
    output.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    output.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
#endif
    output.gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = digital_audio_pins::speaker_bclk, .ws = digital_audio_pins::speaker_word_select, .dout = digital_audio_pins::speaker_data, .din = I2S_GPIO_UNUSED,
        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}};
#if defined(ASSISTANT_BOARD_OTTO)
    if (otto_camera_board()) output.gpio_cfg.din = digital_audio_pins::microphone_data;
#endif
    if (result == ESP_OK) result = i2s_channel_init_std_mode(g_output.channel, &output);
#endif
    if (result == ESP_OK) {
        g_gpio = audio_codec_new_gpio();
        dummy_codec_cfg_t amplifier = {.gpio_if = g_gpio, .pa_pin = digital_audio_pins::amplifier_enable, .pa_reverted = false};
        if (g_gpio) g_amplifier = dummy_codec_new(&amplifier);
        if (!g_gpio || !g_amplifier) result = ESP_ERR_NO_MEM;
        if (result == ESP_OK) {
            if (g_amplifier->mute(g_amplifier, true) != ESP_CODEC_DEV_OK) result = ESP_FAIL;
            g_output_interface.is_open = amplifier_open;
            g_output_interface.enable = amplifier_enable;
            g_output_interface.mute = amplifier_mute;
            g_output_interface.close = amplifier_close;
        }
    }
#if defined(ASSISTANT_BOARD_K10)
    if (result == ESP_OK) {
        const uint8_t address = k10_adc_address();
        if (!address || !bsp_i2c_get_handle()) result = ESP_ERR_INVALID_STATE;
        if (result == ESP_OK) {
            // codec_devctrltakes8bitaddress and shiftsright1. Manufacturer
            // addresses15/11must be suppliedas2A/22,notliteral23or15.
            audio_codec_i2c_cfg_t control = {.port = I2C_NUM_1, .addr = static_cast<uint8_t>(address << 1), .bus_handle = bsp_i2c_get_handle()};
            g_input_control = audio_codec_new_i2c_ctrl(&control);
            if (!g_input_control) result = ESP_ERR_NO_MEM;
        }
        if (result == ESP_OK) {
            es7243e_codec_cfg_t adc = {.ctrl_if = g_input_control};
            g_input_codec = es7243e_codec_new(&adc);
            if (!g_input_codec) result = ESP_FAIL;
        }
    }
#endif
    if (result == ESP_OK) {
        esp_codec_dev_cfg_t input_device = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = nullptr, .data_if = &g_input.interface};
#if defined(ASSISTANT_BOARD_K10)
        input_device.codec_if = g_input_codec;
#endif
        esp_codec_dev_cfg_t output_device = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = &g_output_interface, .data_if = &g_output.interface};
        *microphone = esp_codec_dev_new(&input_device);
        *speaker = esp_codec_dev_new(&output_device);
        if (!*microphone || !*speaker) result = ESP_ERR_NO_MEM;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "audio_init failed: %s", esp_err_to_name(result));
        release_audio(speaker, microphone);
        return result;
    }
#if defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_K10)
    ESP_LOGI(TAG, "digital_audio duplex_port=0 rate=%lu slots=stereo16 microphone=left", static_cast<unsigned long>(board_profile::speaker_sample_rate));
#else
#if defined(ASSISTANT_BOARD_OTTO)
    const int output_port = otto_camera_board() ? 0 : 1;
#else
    constexpr int output_port = 1;
#endif
    ESP_LOGI(TAG, "digital_audio microphone=%lu speaker=%lu input_port=0 output_port=%d analog_gain=unavailable", static_cast<unsigned long>(board_profile::microphone_sample_rate), static_cast<unsigned long>(board_profile::speaker_sample_rate), output_port);
#endif
    return ESP_OK;
}
