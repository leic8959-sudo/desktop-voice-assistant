#include "audio_io.h"
#include "audio_reference_buffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "board_profile.h"
#include "driver/i2s_std.h"
#include "esp_aec.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "model_path.h"

namespace {
constexpr const char *kTag = "audio_io";
#if defined(ASSISTANT_BOARD_OTTO)
uint32_t kCodecRate = 0;
#else
constexpr uint32_t kCodecRate = board_profile::speaker_sample_rate;
static_assert(kCodecRate == 16000 || kCodecRate == 24000, "Unsupported speaker sample rate");
#endif
constexpr uint32_t kMicrophoneRate = board_profile::microphone_sample_rate;
constexpr uint32_t kCaptureRate = 16000;
// 与小智 v2.5.2 Es8311AudioCodec 的模拟输入增益保持一致。
// https://github.com/78/xiaozhi-esp32/blob/v2.5.2/main/audio/codecs/es8311_audio_codec.cc
constexpr float kMicrophoneGainDb =
#if defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_WS_RGB_7C) || defined(ASSISTANT_BOARD_K10)
    37.5f;
#elif defined(ASSISTANT_BOARD_SENSECAP)
    27.0f;
#elif (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO))
    40.0f;
#elif defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_LABPLUS)
    24.0f;
#elif defined(ASSISTANT_BOARD_LICHUANG)
    28.0f;
#else
    30.0f;
#endif
// 已有同步录音证明播放时 12dB 可避免 ADC 削顶；待机使用小智的 30dB。
constexpr float kPlaybackMicrophoneGainDb = 12.0f;
constexpr size_t kMaxReadSamples = 512;
constexpr size_t kResampleTaps = 33;
constexpr size_t kResampleHalf = kResampleTaps / 2;
constexpr size_t kResampleCapacity = board_profile::manual_audio_only ? 512 : 2048;
constexpr size_t kAfeOutputCapacity = board_profile::manual_audio_only ? 2048 : 4096;
constexpr size_t kReferenceCapacity = board_profile::manual_audio_only ? 1 : 8192;
constexpr size_t kReferenceSegments = board_profile::manual_audio_only ? 1 : 256;
constexpr size_t kAecCapacity = 1024;
constexpr size_t kAfeFeedCapacity = 2048;
// 小智 v2.5.2 AudioService::AudioInputTask 每次采集 10ms，再按 AFE chunk 累积。
// https://github.com/78/xiaozhi-esp32/blob/v2.5.2/main/audio/audio_service.cc
constexpr size_t kRawReadSamples = kMicrophoneRate / 100;
constexpr size_t kDiagnosticFrameSamples = 512;
constexpr uint32_t kDiagnosticMaxFrames = 160;
constexpr size_t kDiagnosticChannels = 3;
// The software reference is scheduled slightly after enqueue time. Keep this
// calibration explicit so board measurements can tune it without changing the
// AEC or capture cadence.
constexpr int64_t kReferencePlaybackLeadUs = 7000;
constexpr int64_t kReferenceStaleWindowUs = 250000;

#if (defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_LC_TFT) || defined(ASSISTANT_BOARD_XINGZHI_ABS) || defined(ASSISTANT_BOARD_XINGZHI_METAL) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_GENJUTECH) || ((defined(ASSISTANT_BOARD_MAZA) && !defined(ASSISTANT_BOARD_MAZA_S31)) || ((defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_WS_EPAPER397)) || defined(ASSISTANT_BOARD_ZECTRIX))) || defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH)) || (defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_SPARKBOT)))) || defined(ASSISTANT_BOARD_P4_FUNCTION) || defined(ASSISTANT_BOARD_P4_SPI35)
static i2s_chan_handle_t g_tx;
static i2s_chan_handle_t g_rx;
static const audio_codec_data_if_t *g_data_if;
static const audio_codec_ctrl_if_t *g_ctrl_if;
static const audio_codec_gpio_if_t *g_gpio_if;
static const audio_codec_if_t *g_codec_if;
#endif
static esp_codec_dev_handle_t g_codec;
static esp_codec_dev_handle_t g_microphone;
static uint32_t g_rate;
static bool g_open;
static std::atomic<float> g_microphone_gain_db{0};
static std::atomic<int64_t> g_last_playback_start_us{0};
static SemaphoreHandle_t g_state_lock;
static SemaphoreHandle_t g_playback_lock;
static SemaphoreHandle_t g_audio_buffer_lock;
static SemaphoreHandle_t g_reference_lock;
static SemaphoreHandle_t g_capture_processing_lock;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_codec_open{false};
static std::atomic<bool> g_capture_active{false};
static std::atomic<bool> g_capture_failed{false};
static std::atomic<bool> g_playback_active{false};
static std::atomic<bool> g_output_muted{false};
static std::atomic<bool> g_output_settings_dirty{true};
// Confirmed channel, protected by g_state_lock; notification intent is atomic
// so a timed-out caller can still have the next packet apply its request.
static bool g_applied_notification_output;
static std::atomic<int> g_conversation_volume{80};
static std::atomic<int> g_reminder_volume{80};
static std::atomic<bool> g_conversation_muted{false};
static std::atomic<bool> g_reminders_muted{false};
static std::atomic<bool> g_notification_output{false};
static std::atomic<bool> g_playback_paused{false};
static std::atomic<uint32_t> g_capture_readers{0};
static std::atomic<bool> g_vad_voice_active{false};
static std::atomic<bool> g_afe_reset_pending{false};
static std::atomic<uint32_t> g_afe_generation{0};
static std::atomic<uint32_t> g_reference_generation{0};
static std::atomic<uint32_t> g_reference_delay_samples{0};

static std::atomic<uint32_t> g_metrics_sequence{0};
static std::atomic<bool> g_metrics_valid{false};
static std::atomic<bool> g_metrics_voice_active{false};
static std::atomic<uint32_t> g_metrics_mic_rms{0};
static std::atomic<uint32_t> g_metrics_reference_rms{0};
static std::atomic<uint32_t> g_metrics_clean_rms{0};
static std::atomic<float> g_metrics_echo_correlation{0.0f};
static std::atomic<uint32_t> g_metrics_reference_delay_samples{0};
static std::atomic<bool> g_metrics_playback_active{false};

enum class DiagnosticState : uint32_t {
    Inactive = 0,
    Capturing = 1,
    Ready = 2,
    Reading = 3,
};
static std::atomic<DiagnosticState> g_diagnostic_state{DiagnosticState::Inactive};
static int16_t *g_diagnostic_buffer;
static uint32_t g_diagnostic_frame_count;
static uint32_t g_diagnostic_frames_written;
static size_t g_diagnostic_bytes;
static size_t g_diagnostic_read_offset;

static const esp_afe_sr_iface_t *g_afe_iface;
static esp_afe_sr_data_t *g_afe_data;
static aec_handle_t *g_aec;
static int g_aec_chunk;
static std::atomic<bool> g_aec_ready{false};
#if !defined(ASSISTANT_BOARD_INTERNAL_RAM)
static srmodel_list_t *g_models;
#endif

static std::atomic<uint64_t> g_mic_frames{0};
static std::atomic<uint64_t> g_reference_frames{0};
static std::atomic<uint64_t> g_output_frames{0};
static std::atomic<uint64_t> g_reference_underflows{0};
static std::atomic<uint64_t> g_reference_stale_drops{0};
static std::atomic<uint64_t> g_capture_overflows{0};
static std::atomic<uint64_t> g_mic_energy{0};
static std::atomic<uint64_t> g_reference_energy{0};
static std::atomic<uint64_t> g_clean_energy{0};
static std::atomic<uint64_t> g_mic_square{0};
static std::atomic<uint64_t> g_reference_square{0};
static std::atomic<uint64_t> g_clean_square{0};
static std::atomic<uint64_t> g_mic_samples{0};
static std::atomic<uint64_t> g_reference_samples{0};
static std::atomic<uint64_t> g_clean_samples{0};
static int64_t g_last_stats_us;

template<uint32_t InputRate>
struct FirResampler {
    static_assert(InputRate == 16000 || InputRate == 24000 || InputRate == 32000, "Capture input must be16/24/32kHz");
    static constexpr size_t kInputNumerator = InputRate == 16000 ? 1 : InputRate == 24000 ? 3 : 2;
    static constexpr size_t kOutputDenominator = InputRate == 24000 ? 2 : 1;
    std::array<int16_t, kResampleCapacity> input{};
    size_t count;
    int64_t input_start;
    uint64_t next_output;
    std::array<std::array<int32_t, kResampleTaps>, kOutputDenominator> coefficients{};
    bool coefficients_ready = false;

    void reset() {
        input.fill(0);
        count = InputRate == 16000 ? 0 : kResampleHalf;
        input_start = InputRate == 16000 ? 0 : -static_cast<int64_t>(kResampleHalf);
        next_output = 0;
    }

    void initialize_coefficients() {
        if (coefficients_ready) return;
        constexpr float pi = 3.14159265358979323846f;
        // Preserve the existing24kHz coefficients;32kHz has a different Nyquist
        // ratio and an integer2:1 timeline rather than the24kHz3:2 timeline.
        constexpr float cutoff = InputRate == 24000 ? 1.0f / 3.0f : 0.25f;
        for (size_t phase = 0; phase < kOutputDenominator; ++phase) {
            float sum = 0.0f;
            const float fraction = static_cast<float>(phase) / kOutputDenominator;
            for (size_t tap = 0; tap < kResampleTaps; ++tap) {
                const float x = static_cast<float>(tap) - static_cast<float>(kResampleHalf) - fraction;
                const float sinc = std::fabs(x) < 0.0001f ? 1.0f : std::sin(2.0f * pi * cutoff * x) / (2.0f * pi * cutoff * x);
                const float window = 0.54f + 0.46f * std::cos(pi * x / static_cast<float>(kResampleHalf));
                const float value = 2.0f * cutoff * sinc * window;
                coefficients[phase][tap] = static_cast<int32_t>(value * 1073741824.0f);
                sum += value;
            }
            for (size_t tap = 0; tap < kResampleTaps; ++tap) {
                coefficients[phase][tap] = static_cast<int32_t>(static_cast<float>(coefficients[phase][tap]) / sum);
            }
        }
        coefficients_ready = true;
    }

    bool append(const int16_t *samples, size_t sample_count) {
        if (!samples || sample_count == 0) return true;
        if constexpr (InputRate == 16000) {
            if (sample_count > kResampleCapacity - count) return false;
            std::memcpy(input.data() + count, samples, sample_count * sizeof(int16_t));
            count += sample_count;
            return true;
        }
        initialize_coefficients();
        if (sample_count > kResampleCapacity) return false;
        const int64_t safe_drop = static_cast<int64_t>(next_output * kInputNumerator / kOutputDenominator) - static_cast<int64_t>(kResampleHalf);
        if (safe_drop > input_start) discard_before(safe_drop);
        if (count + sample_count > kResampleCapacity) return false;
        std::memcpy(input.data() + count, samples, sample_count * sizeof(int16_t));
        count += sample_count;
        return true;
    }

    size_t produce(int16_t *output, size_t capacity) {
        if (!output || capacity == 0) return 0;
        if constexpr (InputRate == 16000) {
            const size_t produced = std::min(capacity, count);
            std::memcpy(output, input.data(), produced * sizeof(int16_t));
            count -= produced;
            std::memmove(input.data(), input.data() + produced, count * sizeof(int16_t));
            return produced;
        }
        initialize_coefficients();
        size_t produced = 0;
        while (produced < capacity) {
            const int64_t position_num = static_cast<int64_t>(next_output) * kInputNumerator;
            const int64_t center = position_num / kOutputDenominator;
            const size_t phase = static_cast<size_t>(position_num % kOutputDenominator);
            if (center - static_cast<int64_t>(kResampleHalf) < input_start ||
                center + static_cast<int64_t>(kResampleHalf) >= input_start + static_cast<int64_t>(count)) break;
            int64_t sum = 0;
            for (size_t tap = 0; tap < kResampleTaps; ++tap) {
                const int64_t sample_index = center + static_cast<int64_t>(tap) - static_cast<int64_t>(kResampleHalf);
                sum += static_cast<int64_t>(input[static_cast<size_t>(sample_index - input_start)]) * coefficients[phase][tap];
            }
            int64_t value = (sum + (1LL << 29)) >> 30;
            value = std::max<int64_t>(-32768, std::min<int64_t>(32767, value));
            output[produced++] = static_cast<int16_t>(value);
            ++next_output;
        }
        const int64_t keep_from = static_cast<int64_t>(next_output * kInputNumerator / kOutputDenominator) - static_cast<int64_t>(kResampleHalf);
        if (keep_from > input_start) discard_before(keep_from);
        return produced;
    }

private:
    void discard_before(int64_t position) {
        if (position <= input_start) return;
        const size_t discard = static_cast<size_t>(std::min<int64_t>(position - input_start, static_cast<int64_t>(count)));
        if (discard == 0) return;
        std::memmove(input.data(), input.data() + discard, (count - discard) * sizeof(int16_t));
        input_start += static_cast<int64_t>(discard);
        count -= discard;
    }
};

EXT_RAM_BSS_ATTR static FirResampler<kMicrophoneRate> g_capture_resampler;
// Network playback remains PCM24 even on a native16kHz speaker.
EXT_RAM_BSS_ATTR static FirResampler<24000> g_reference_resampler;
EXT_RAM_BSS_ATTR static int16_t g_aec_pending[kAecCapacity];
EXT_RAM_BSS_ATTR static int16_t g_aec_original_mic[kAecCapacity];
EXT_RAM_BSS_ATTR static int16_t g_aec_original_reference[kAecCapacity];
static size_t g_aec_pending_count;
EXT_RAM_BSS_ATTR static int16_t g_afe_feed_pending[kAfeFeedCapacity];
static size_t g_afe_feed_pending_count;
EXT_RAM_BSS_ATTR static AudioReferenceBuffer<kReferenceCapacity, kCaptureRate, kReferenceSegments> g_reference_buffer;
static int64_t g_reference_tail_due_us;
EXT_RAM_BSS_ATTR static int16_t g_afe_output_ring[kAfeOutputCapacity];
// VAD 标签与 PCM 使用相同索引和互斥锁；0 未知，1 静音，2 语音。
EXT_RAM_BSS_ATTR static uint8_t g_afe_output_vad[kAfeOutputCapacity];
static size_t g_afe_output_read;
static size_t g_afe_output_count;
static uint32_t g_afe_output_generation;
static uint32_t g_afe_output_capture_generation;
static uint64_t g_afe_output_next_sample;
static bool g_afe_output_discontinuous = true;
static int16_t *g_aec_input_frame;
static int16_t *g_aec_reference_frame;
static int16_t *g_aec_cleaned_frame;

static void diagnostic_discard_locked() {
    if (g_diagnostic_buffer) heap_caps_free(g_diagnostic_buffer);
    g_diagnostic_buffer = nullptr;
    g_diagnostic_frame_count = 0;
    g_diagnostic_frames_written = 0;
    g_diagnostic_bytes = 0;
    g_diagnostic_read_offset = 0;
    g_diagnostic_state.store(DiagnosticState::Inactive, std::memory_order_release);
}

static void diagnostic_capture_frame(const int16_t *mic, const int16_t *reference, const int16_t *clean) {
    if (!mic || !reference || !clean || g_diagnostic_state.load(std::memory_order_relaxed) != DiagnosticState::Capturing ||
        !g_diagnostic_buffer || g_diagnostic_frames_written >= g_diagnostic_frame_count) return;
    const size_t frame_offset = static_cast<size_t>(g_diagnostic_frames_written) * kDiagnosticFrameSamples * kDiagnosticChannels;
    int16_t *destination = g_diagnostic_buffer + frame_offset;
    for (size_t sample = 0; sample < kDiagnosticFrameSamples; ++sample) {
        destination[(sample * kDiagnosticChannels) + 0] = mic[sample];
        destination[(sample * kDiagnosticChannels) + 1] = reference[sample];
        destination[(sample * kDiagnosticChannels) + 2] = clean[sample];
    }
    ++g_diagnostic_frames_written;
    if (g_diagnostic_frames_written == g_diagnostic_frame_count) {
        g_diagnostic_state.store(DiagnosticState::Ready, std::memory_order_release);
    }
}

#if (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP)))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH))) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
static esp_err_t set_external_amplifier(bool enabled) {
#if defined(ASSISTANT_BOARD_SPOT_C5)
    return spot_c5_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_MOJI2_C5)
    return moji2_c5_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_C5_LCD169)
    return c5_lcd169_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_STOPWATCH)
    return stopwatch_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_WS35)
    return waveshare_35_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || defined(ASSISTANT_BOARD_QUANDONG)
    return gpio_set_level(GPIO_NUM_1, enabled ? 0 : 1);
#elif defined(ASSISTANT_BOARD_KEVIN_RGB)
    return gpio_set_level(GPIO_NUM_45, enabled ? 1 : 0);
#elif defined(ASSISTANT_BOARD_CHD_EYE)
    return gpio_set_level(GPIO_NUM_40, enabled ? 1 : 0);
#elif defined(ASSISTANT_BOARD_STICK_S3)
    return stick_s3_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_ECHO_PYRAMID)
    return echo_pyramid_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE))
    return atomic_echo_base_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_SENSECAP)
    return sensecap_set_amplifier(enabled);
#elif (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))
    return box3_set_amplifier(enabled);
#elif ((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))
    return amoled216_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_MOSAICO)
    return mosaico_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_M5_COREP4)
    return m5_corep4_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_M5_TAB5)
    return m5_tab5_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_P4_MIPI) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_POWER_AMP_IO, enabled ? 1 : 0), kTag, "direct PA level");
    return gpio_get_level(BSP_POWER_AMP_IO) == (enabled ? 1 : 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
#elif defined(ASSISTANT_BOARD_WS_CAM)
    return waveshare_cam_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_KORVO2)
    return gpio_set_level(GPIO_NUM_48, enabled ? 1 : 0);
#elif defined(ASSISTANT_BOARD_WS143C)
    return gpio_set_level(GPIO_NUM_46, enabled ? 1 : 0);
#elif defined(ASSISTANT_BOARD_WS349)
    return waveshare_349_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_VOCAT)
    return vocat_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_LCD_EV)
    return lcd_ev_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_WS4B)
    return waveshare_4b_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_WS_RGB_C)
    return waveshare_rgb_c_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_CHD)
    return gpio_set_level(GPIO_NUM_46, enabled ? 1 : 0);
#elif defined(ASSISTANT_BOARD_WS_AUDIO)
    return waveshare_audio_board_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_ATK_BOX3)
    return atk_box3_set_amplifier(enabled);
#elif (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO))
    return atk_box2_set_amplifier(enabled);
#elif defined(ASSISTANT_BOARD_ATK_DEV)
    return atk_xl9555_set_amplifier(enabled);
#else
    return pca9557_box_set_amplifier(enabled);
#endif
}
#endif

static bool microphone_gain_supported() {
#if defined(ASSISTANT_BOARD_ATK_BOX)
    return atk_box_audio() == AtkBoxAudio::Es8311;
#else
    return board_profile::microphone_gain;
#endif
}

static int set_microphone_gain(float gain) {
#if defined(ASSISTANT_BOARD_CORES3)
    return waveshare_spi_audio_set_microphone_gain(gain);
#elif defined(ASSISTANT_BOARD_K10)
    return digital_audio_set_microphone_gain(gain);
#else
#if defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH)) || (defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_SPARKBOT)))
    return g_codec_if && g_codec_if->set_mic_gain ? g_codec_if->set_mic_gain(g_codec_if, gain) : ESP_CODEC_DEV_WRONG_STATE;
#elif (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || (defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP))))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
    return waveshare_spi_audio_set_microphone_gain(gain);
#else
    return esp_codec_dev_set_in_gain(g_microphone, gain);
#endif
#endif
}

static int set_output_mute(bool muted) {
#if defined(ASSISTANT_BOARD_CORES3)
    const int result = esp_codec_dev_set_out_mute(g_codec, muted);
    if (result != ESP_CODEC_DEV_OK) return result;
    return waveshare_spi_audio_set_output_mute(muted);
#elif defined(ASSISTANT_BOARD_SENSAIR_C5)
    return sensair_audio_set_mute(muted);
#else
#if (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || (defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP))))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH))) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
    // Mute cuts the external PA first. Unmute enables it only after the codec
    // request succeeds. Each backend retains its own ACK/readback limits.
    const esp_err_t amplifier_result = muted ? set_external_amplifier(false) : ESP_OK;
#endif
    const int result = esp_codec_dev_set_out_mute(g_codec, muted);
#if defined(ASSISTANT_BOARD_MVSRLORA)
    if (result != ESP_CODEC_DEV_OK) return result;
    // codec_dev1.5.11 discards the codec mute callback result. Check the
    // actual digital PA wrapper so a failed GPIO41 readback is not ACKed.
    return digital_audio_set_output_mute(muted);
#endif
#if (defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_SPARKBOT))
    if (result != ESP_CODEC_DEV_OK) return result;
    return g_codec_if && g_codec_if->mute ? g_codec_if->mute(g_codec_if, muted) : ESP_CODEC_DEV_WRONG_STATE;
#endif
#if (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || (defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP))))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH))) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
    if (result != ESP_CODEC_DEV_OK) return result;
    // Keep codec_dev's mute state, then check the actual DAC callback result
    // that codec_dev1.5.11 discarded. Never enable the PA after a DAC failure.
#if defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH)))
    const int codec_result = g_codec_if && g_codec_if->mute ? g_codec_if->mute(g_codec_if, muted) : ESP_CODEC_DEV_WRONG_STATE;
#else
    const int codec_result = waveshare_spi_audio_set_output_mute(muted);
#endif
    if (codec_result != ESP_CODEC_DEV_OK) return codec_result;
    if (amplifier_result != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
    if (!muted && set_external_amplifier(true) != ESP_OK) {
        // A write may have succeeded before its readback failed. Try to cut
        // the PA again, while still returning the original enable failure.
        (void)set_external_amplifier(false);
        return ESP_CODEC_DEV_DRV_ERR;
    }
#elif defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS)
    if (result != ESP_CODEC_DEV_OK) return result;
    return waveshare_spi_audio_set_output_mute(muted);
#endif
    return result;
#endif
}

static esp_err_t close_codecs() {
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
    const esp_err_t amplifier_result = sensair_audio_set_mute(true) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
#elif (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || (defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP))))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH))) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
    const esp_err_t amplifier_result = set_external_amplifier(false);
#else
    const esp_err_t amplifier_result = ESP_OK;
#endif
    const int microphone_result = g_microphone != g_codec ? esp_codec_dev_close(g_microphone) : ESP_CODEC_DEV_OK;
#if (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_SPARKBOT)
    // ES8389 RX depends onTX;SparkBot's sharedBL/PA cannot gate its amplifier.
    // Keep the data clocks open and mute the actual DAC between sessions.
    const int output_result = set_output_mute(true);
#else
    const int output_result = esp_codec_dev_close(g_codec);
#endif
    return amplifier_result == ESP_OK && microphone_result == ESP_CODEC_DEV_OK && output_result == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static int set_output_volume(int volume) {
#if defined(ASSISTANT_BOARD_STICK_S3)
    // Keep logical volume intact for settings/UI and scale every hardware
    // path, including reminder playback and local key updates, like upstream.
    volume = (volume * 60 + 50) / 100;
#endif
    return esp_codec_dev_set_out_vol(g_codec, volume);
}

static bool requested_output_mute(bool notification) {
    return g_playback_paused.load() || (notification ? g_reminders_muted.load() : g_conversation_muted.load());
}

static esp_err_t apply_output_settings_locked() {
    g_output_settings_dirty = true;
    if (!g_codec || !g_open) return ESP_OK;
    const bool notification = g_notification_output.load();
    const bool muted = requested_output_mute(notification);
    const int volume = notification ? g_reminder_volume.load() : g_conversation_volume.load();
    const int volume_result = set_output_volume(volume);
    const int mute_result = set_output_mute(muted);
    if (volume_result != ESP_CODEC_DEV_OK || mute_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "output_settings_failed notification=%d volume_result=%d mute_result=%d", notification, volume_result, mute_result);
        return ESP_FAIL;
    }
    g_applied_notification_output = notification;
    g_output_muted = muted;
    g_output_settings_dirty = false;
    return ESP_OK;
}

static esp_err_t codec_open_locked(uint32_t rate) {
    if (!g_codec || !g_microphone) return ESP_ERR_INVALID_STATE;
    if (g_open && g_rate == rate) {
        return ESP_OK;
    }
    if (g_open) {
        if (close_codecs() != ESP_OK) return ESP_FAIL;
        g_open = false;
        g_codec_open = false;
    }
    esp_codec_dev_sample_info_t sample = {};
    sample.sample_rate = rate;
    sample.channel = 1;
    sample.bits_per_sample = 16;
#if defined(ASSISTANT_BOARD_AIVOX3)
    sample.mclk_multiple = 128;
#else
    sample.mclk_multiple = 256;
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX)
    if (g_microphone != g_codec) {
        sample.sample_rate = kMicrophoneRate;
        if (esp_codec_dev_open(g_microphone, &sample) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    }
#else
    if (board_profile::separate_microphone) {
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
        sample.sample_rate = 16000; sample.channel = 1; sample.channel_mask = 1;
#elif defined(ASSISTANT_BOARD_TCIRCLE) || (defined(ASSISTANT_BOARD_TAIJI) || (defined(ASSISTANT_BOARD_TCAMERA) || (defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_K10)))) || ((defined(ASSISTANT_BOARD_HEADLESS) && !defined(ASSISTANT_BOARD_SPOT_C5)) && !defined(ASSISTANT_BOARD_ATOM_ECHOS3R) && !defined(ASSISTANT_BOARD_ATOM_M12_BASE) && !((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31))) || defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || (defined(ASSISTANT_BOARD_OLED) && !defined(ASSISTANT_BOARD_KEVIN_BOX2))
        sample.sample_rate = kMicrophoneRate;
#else
        // Select MIC1 from the board's physical ES7210 slots, keeping the mono
        // resampler/software-AEC contract; this does not claim beamforming.
#if defined(ASSISTANT_BOARD_SENSECAP) || defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE) || (defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)
        // These ports initialize standard stereo RX, not the TDM4 backend.
        sample.channel = 2;
#elif (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS)
        sample.channel = 1;
#elif (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP)))) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
        sample.channel = 4;
#else
        sample.channel = 2;
#endif
#if !(defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) && !defined(ASSISTANT_BOARD_MIXGO)
#if defined(ASSISTANT_BOARD_SENSECAP)
        sample.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
#else
        sample.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0);
#endif
#endif
#endif
        if (esp_codec_dev_open(g_microphone, &sample) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    }
#endif
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
    sample.sample_rate = kCodecRate; sample.channel = 1; sample.channel_mask = 0;
#endif
#if defined(ASSISTANT_BOARD_TCIRCLE) || (defined(ASSISTANT_BOARD_TAIJI) || (defined(ASSISTANT_BOARD_TCAMERA) || (defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_K10)))) || ((defined(ASSISTANT_BOARD_HEADLESS) && !defined(ASSISTANT_BOARD_SPOT_C5)) && !defined(ASSISTANT_BOARD_ATOM_ECHOS3R) && !defined(ASSISTANT_BOARD_ATOM_M12_BASE) && !((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31))) || defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || (defined(ASSISTANT_BOARD_OLED) && !defined(ASSISTANT_BOARD_KEVIN_BOX2))
    sample.sample_rate = rate;
#endif
#if (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP)))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO)
    // Reset the physical input selection before opening the speaker.
    // 7C's ES8389 output uses stereo, with mono duplicated at its boundary.
#if defined(ASSISTANT_BOARD_WS_RGB_7C) || defined(ASSISTANT_BOARD_S31_KORVO)
    sample.channel = 2;
#else
    sample.channel = 1;
#endif
    sample.channel_mask = 0;
#endif
    if (esp_codec_dev_open(g_codec, &sample) != ESP_CODEC_DEV_OK) {
        if (g_microphone != g_codec) (void)esp_codec_dev_close(g_microphone);
        return ESP_FAIL;
    }
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
    if (sensair_audio_status() != ESP_OK) { (void)close_codecs(); return ESP_FAIL; }
#elif defined(ASSISTANT_BOARD_ATK_BOX)
    if (atk_box_audio() == AtkBoxAudio::Digital && digital_audio_open_status() != ESP_OK) {
        (void)close_codecs();
        return ESP_FAIL;
    }
#elif defined(ASSISTANT_BOARD_TCIRCLE) || (defined(ASSISTANT_BOARD_TAIJI) || (defined(ASSISTANT_BOARD_TCAMERA) || (defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_K10)))) || ((defined(ASSISTANT_BOARD_HEADLESS) && !defined(ASSISTANT_BOARD_SPOT_C5)) && !defined(ASSISTANT_BOARD_ATOM_ECHOS3R) && !defined(ASSISTANT_BOARD_ATOM_M12_BASE) && !((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31))) || defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || (defined(ASSISTANT_BOARD_OLED) && !defined(ASSISTANT_BOARD_KEVIN_BOX2))
    // esp_codec_dev1.5.11 ignores data-interface set_fmt/enable return values.
    // Check actual channel state before accepting an open digital audio path.
    if (digital_audio_open_status() != ESP_OK) {
        (void)close_codecs();
        return ESP_FAIL;
    }
#elif (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_WAVESHARE_SPI) || defined(ASSISTANT_BOARD_LCD_185B) || defined(ASSISTANT_BOARD_LCD_185C) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP)))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO) || defined(ASSISTANT_BOARD_CORES3)
    if (waveshare_spi_audio_open_status() != ESP_OK) {
        (void)close_codecs();
        return ESP_FAIL;
    }
#endif
    const bool notification = g_notification_output.load();
    const bool muted = requested_output_mute(notification);
    if ((microphone_gain_supported() && set_microphone_gain(kMicrophoneGainDb) != ESP_CODEC_DEV_OK) ||
        set_output_volume(notification ? g_reminder_volume.load() : g_conversation_volume.load()) != ESP_CODEC_DEV_OK ||
        set_output_mute(muted) != ESP_CODEC_DEV_OK) {
        (void)close_codecs();
        return ESP_FAIL;
    }
    g_open = true;
    g_codec_open = true;
    g_applied_notification_output = notification;
    g_output_muted = muted;
    g_output_settings_dirty = false;
    g_rate = rate;
    g_microphone_gain_db = microphone_gain_supported() ? kMicrophoneGainDb : 0;
    ESP_LOGI(kTag, "microphone_gain_supported=%d microphone_gain_db=%.0f", microphone_gain_supported(), static_cast<double>(g_microphone_gain_db.load()));
    return ESP_OK;
}

static void maybe_close_codec() {
    if (g_capture_active.load() || g_playback_active.load() || g_capture_readers.load() != 0) return;
    if (!g_state_lock || xSemaphoreTake(g_state_lock, portMAX_DELAY) != pdTRUE) return;
    if (!g_capture_active.load() && !g_playback_active.load() && g_capture_readers.load() == 0 && g_open) {
        (void)close_codecs();
        g_open = false;
        g_codec_open = false;
    }
    xSemaphoreGive(g_state_lock);
}

static bool acquire_capture_reader() {
    if (!g_state_lock || xSemaphoreTake(g_state_lock, portMAX_DELAY) != pdTRUE) return false;
    const bool active = g_capture_active.load() && g_open;
    if (active) g_capture_readers.fetch_add(1);
    xSemaphoreGive(g_state_lock);
    return active;
}

static void release_capture_reader() {
    if (g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
        g_capture_readers.fetch_sub(1);
        xSemaphoreGive(g_state_lock);
    } else {
        g_capture_readers.fetch_sub(1);
    }
}

static void clear_reference() {
    if (!g_reference_lock || xSemaphoreTake(g_reference_lock, portMAX_DELAY) != pdTRUE) return;
    g_reference_resampler.reset();
    g_reference_buffer.clear();
    g_reference_tail_due_us = 0;
    xSemaphoreGive(g_reference_lock);
}

[[maybe_unused]] static bool push_reference(const int16_t *samples, size_t sample_count, bool muted) {
    if (!samples || sample_count == 0 || !g_reference_lock) return false;
    if (xSemaphoreTake(g_reference_lock, portMAX_DELAY) != pdTRUE) return false;
    if (sample_count > kReferenceCapacity) {
        samples += sample_count - kReferenceCapacity;
        sample_count = kReferenceCapacity;
    }
    const int64_t now_us = esp_timer_get_time();
    const int64_t minimum_start_us = now_us + kReferencePlaybackLeadUs;
    const int64_t start_us = std::max(minimum_start_us, g_reference_tail_due_us);
    if (!g_reference_buffer.push(samples, sample_count, start_us, muted)) {
        xSemaphoreGive(g_reference_lock);
        ESP_LOGE(kTag, "reference_schedule_full; playback packet rejected");
        return false;
    }
    g_reference_tail_due_us = start_us + static_cast<int64_t>((static_cast<uint64_t>(sample_count) * 1000000ULL) / kCaptureRate);
    g_reference_frames.fetch_add(sample_count);
    xSemaphoreGive(g_reference_lock);
    return true;
}

static bool pop_reference(int16_t *samples, size_t sample_count, int64_t capture_start_us, uint32_t *delay_samples) {
    if (!samples || sample_count == 0) return false;
    std::memset(samples, 0, sample_count * sizeof(int16_t));
    if (delay_samples) *delay_samples = g_reference_delay_samples.load();
    if (!g_reference_lock || xSemaphoreTake(g_reference_lock, portMAX_DELAY) != pdTRUE) return true;
    const bool echo_expected = g_playback_active.load() || (g_reference_tail_due_us > 0 && capture_start_us <= g_reference_tail_due_us + kReferenceStaleWindowUs);
    size_t reference_copied = 0;
    uint64_t reference_energy = 0;
    uint64_t reference_square = 0;
    uint64_t delay_sum = 0;
    for (size_t slot = 0; slot < sample_count; ++slot) {
        const int64_t slot_time_us = capture_start_us + static_cast<int64_t>((static_cast<uint64_t>(slot) * 1000000ULL) / kCaptureRate);
        while (g_reference_buffer.size() > 0 && g_reference_buffer.front_due_us() + kReferenceStaleWindowUs < slot_time_us) {
            g_reference_buffer.pop();
            g_reference_stale_drops.fetch_add(1);
        }
        if (g_reference_buffer.size() == 0 || g_reference_buffer.front_due_us() > slot_time_us) {
            continue;
        }
        samples[slot] = g_reference_buffer.front_sample();
        const int64_t delay_us = std::max<int64_t>(0, slot_time_us - g_reference_buffer.front_due_us());
        delay_sum += static_cast<uint64_t>((delay_us * static_cast<int64_t>(kCaptureRate)) / 1000000);
        reference_energy += static_cast<uint16_t>(samples[slot] < 0 ? -samples[slot] : samples[slot]);
        const int32_t value = samples[slot];
        reference_square += static_cast<uint64_t>(value * value);
        g_reference_buffer.pop();
        ++reference_copied;
    }
    g_reference_energy.fetch_add(reference_energy);
    g_reference_square.fetch_add(reference_square);
    g_reference_samples.fetch_add(reference_copied);
    if (delay_samples && reference_copied > 0) {
        *delay_samples = static_cast<uint32_t>(delay_sum / reference_copied);
        g_reference_delay_samples.store(*delay_samples);
    }
    if (g_playback_active.load() && reference_copied < sample_count && g_reference_buffer.size() == 0) {
        g_reference_underflows.fetch_add(sample_count - reference_copied);
    }
    xSemaphoreGive(g_reference_lock);
    return echo_expected;
}

static void clear_afe_output_locked() {
    g_afe_output_read = 0;
    g_afe_output_count = 0;
    ++g_afe_output_generation;
    g_afe_output_discontinuous = true;
}

static void clear_audio_buffers() {
    if (g_capture_processing_lock && xSemaphoreTake(g_capture_processing_lock, portMAX_DELAY) != pdTRUE) return;
    diagnostic_discard_locked();
    g_capture_failed = false;
    if (g_audio_buffer_lock && xSemaphoreTake(g_audio_buffer_lock, portMAX_DELAY) == pdTRUE) {
        clear_afe_output_locked();
        xSemaphoreGive(g_audio_buffer_lock);
    }
    g_capture_resampler.reset();
    g_aec_pending_count = 0;
    g_afe_feed_pending_count = 0;
    if (g_capture_processing_lock) xSemaphoreGive(g_capture_processing_lock);
}

static void push_afe_output(const int16_t *samples, size_t sample_count, uint8_t vad, uint32_t fetch_generation) {
    if (!samples || sample_count == 0 || !g_audio_buffer_lock) return;
    if (xSemaphoreTake(g_audio_buffer_lock, portMAX_DELAY) != pdTRUE) return;
    if (fetch_generation != g_afe_generation.load() || g_afe_reset_pending.load() || !g_capture_active.load()) {
        xSemaphoreGive(g_audio_buffer_lock);
        return;
    }
    g_afe_output_next_sample += sample_count;
    g_afe_output_capture_generation = fetch_generation;
    const size_t dropped = g_afe_output_count + sample_count > kAfeOutputCapacity ? g_afe_output_count + sample_count - kAfeOutputCapacity : 0;
    if (dropped) {
        if constexpr (board_profile::manual_audio_only) g_capture_failed = true;
        ++g_afe_output_generation;
        g_afe_output_discontinuous = true;
        g_capture_overflows.fetch_add(dropped);
    }
    if (sample_count > kAfeOutputCapacity) {
        samples += sample_count - kAfeOutputCapacity;
        sample_count = kAfeOutputCapacity;
    }
    while (g_afe_output_count + sample_count > kAfeOutputCapacity) {
        g_afe_output_read = (g_afe_output_read + 1) % kAfeOutputCapacity;
        --g_afe_output_count;
    }
    for (size_t index = 0; index < sample_count; ++index) {
        const size_t write_index = (g_afe_output_read + g_afe_output_count) % kAfeOutputCapacity;
        g_afe_output_ring[write_index] = samples[index];
        g_afe_output_vad[write_index] = vad;
        ++g_afe_output_count;
    }
    xSemaphoreGive(g_audio_buffer_lock);
}

static size_t pop_afe_output(int16_t *samples, size_t capacity, AudioCaptureFrame *frame) {
    if (!samples || capacity == 0 || !g_audio_buffer_lock) return 0;
    if (xSemaphoreTake(g_audio_buffer_lock, portMAX_DELAY) != pdTRUE) return 0;
    if (g_afe_output_capture_generation != g_afe_generation.load() || g_afe_reset_pending.load() || !g_capture_active.load()) {
        xSemaphoreGive(g_audio_buffer_lock);
        return 0;
    }
    size_t copied = std::min(capacity, g_afe_output_count);
    if (copied && frame) {
        const uint8_t vad = g_afe_output_vad[g_afe_output_read];
        size_t same_vad = 1;
        while (same_vad < copied && g_afe_output_vad[(g_afe_output_read + same_vad) % kAfeOutputCapacity] == vad) ++same_vad;
        copied = same_vad;
        frame->vad_valid = vad == 1 || vad == 2;
        frame->voice_active = vad == 2;
        frame->continuous = !g_afe_output_discontinuous;
        frame->generation = g_afe_output_generation;
        frame->sample_index = g_afe_output_next_sample - g_afe_output_count;
    }
    if (copied) g_afe_output_discontinuous = false;
    for (size_t index = 0; index < copied; ++index) {
        samples[index] = g_afe_output_ring[g_afe_output_read];
        g_afe_output_read = (g_afe_output_read + 1) % kAfeOutputCapacity;
    }
    g_afe_output_count -= copied;
    if (g_afe_output_count == 0) g_afe_output_read = 0;
    xSemaphoreGive(g_audio_buffer_lock);
    return copied;
}

[[maybe_unused]] static void afe_task(void *) {
    while (true) {
        if (!g_afe_iface || !g_afe_data) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (g_afe_reset_pending.exchange(false)) {
            if (g_capture_processing_lock) xSemaphoreTake(g_capture_processing_lock, portMAX_DELAY);
            (void)g_afe_iface->reset_buffer(g_afe_data);
            g_vad_voice_active = false;
            if (g_audio_buffer_lock && xSemaphoreTake(g_audio_buffer_lock, portMAX_DELAY) == pdTRUE) {
                clear_afe_output_locked();
                xSemaphoreGive(g_audio_buffer_lock);
            }
            if (g_capture_processing_lock) xSemaphoreGive(g_capture_processing_lock);
        }
        const uint32_t fetch_generation = g_afe_generation.load();
        afe_fetch_result_t *result = g_afe_iface->fetch_with_delay(g_afe_data, pdMS_TO_TICKS(100));
        if (!result || result->ret_value != ESP_OK || fetch_generation != g_afe_generation.load() || g_afe_reset_pending.load() || !g_capture_active.load()) continue;
        g_vad_voice_active = result->vad_state == VAD_SPEECH;
        const uint8_t vad = result->vad_state == VAD_SPEECH ? 2 : result->vad_state == VAD_SILENCE ? 1 : 0;
        if (result->data && result->data_size > 0) push_afe_output(result->data, static_cast<size_t>(result->data_size) / sizeof(int16_t), vad, fetch_generation);
    }
}

static bool feed_afe(const int16_t *samples, size_t sample_count) {
    if (!g_afe_iface || !g_afe_data || g_afe_reset_pending.load()) return false;
    if (sample_count > 0) {
        if (g_afe_feed_pending_count + sample_count > kAfeFeedCapacity) return false;
        std::memcpy(g_afe_feed_pending + g_afe_feed_pending_count, samples, sample_count * sizeof(int16_t));
        g_afe_feed_pending_count += sample_count;
    }
    const size_t chunk = static_cast<size_t>(g_afe_iface->get_feed_chunksize(g_afe_data));
    if (chunk == 0 || chunk > kAfeFeedCapacity) return false;
    while (g_afe_feed_pending_count >= chunk) {
        if (g_afe_iface->feed(g_afe_data, g_afe_feed_pending) < 0) return false;
        const size_t remaining = g_afe_feed_pending_count - chunk;
        std::memmove(g_afe_feed_pending, g_afe_feed_pending + chunk, remaining * sizeof(int16_t));
        g_afe_feed_pending_count = remaining;
    }
    return true;
}

static void publish_capture_metrics(const int16_t *mic, const int16_t *reference, const int16_t *clean, size_t sample_count, uint32_t delay_samples) {
    if (!mic || !reference || !clean || sample_count == 0) return;
    uint64_t mic_square = 0;
    uint64_t reference_square = 0;
    uint64_t clean_square = 0;
    int64_t correlation_sum = 0;
    for (size_t index = 0; index < sample_count; ++index) {
        const int64_t mic_value = mic[index];
        const int64_t reference_value = reference[index];
        const int64_t clean_value = clean[index];
        mic_square += static_cast<uint64_t>(mic_value * mic_value);
        reference_square += static_cast<uint64_t>(reference_value * reference_value);
        clean_square += static_cast<uint64_t>(clean_value * clean_value);
        correlation_sum += mic_value * reference_value;
    }
    const double correlation_denominator = std::sqrt(static_cast<double>(mic_square) * static_cast<double>(reference_square));
    // Correlation is a candidate signal for the protocol layer, not ERLE or
    // an acoustic acceptance result.
    const float correlation = correlation_denominator > 0.0
        ? static_cast<float>(std::max(-1.0, std::min(1.0, static_cast<double>(correlation_sum) / correlation_denominator)))
        : 0.0f;
    float delayed_correlation = std::fabs(correlation);
    // 语音参考有小量时序误差，单点相关会把同一声音误判为不相关。
    if (sample_count == 512 && reference_square > 512 * 64ULL && mic_square > 512 * 64ULL) {
        int64_t mic_power = 0;
        for (int index = 128; index < 384; ++index) mic_power += static_cast<int64_t>(mic[index]) * mic[index];
        for (int lag = -128; lag <= 128; lag += 4) {
            int64_t dot = 0, ref_power = 0;
            for (int index = 128; index < 384; ++index) {
                const int32_t ref = reference[index + lag];
                dot += static_cast<int64_t>(mic[index]) * ref;
                ref_power += static_cast<int64_t>(ref) * ref;
            }
            const double denominator = std::sqrt(static_cast<double>(mic_power) * ref_power);
            if (denominator > 0) delayed_correlation = std::max(delayed_correlation, static_cast<float>(std::fabs(static_cast<double>(dot) / denominator)));
        }
    }
    g_metrics_sequence.fetch_add(1, std::memory_order_acq_rel);
    g_metrics_voice_active.store(g_vad_voice_active.load(), std::memory_order_relaxed);
    g_metrics_mic_rms.store(static_cast<uint32_t>(std::sqrt(static_cast<double>(mic_square) / sample_count)), std::memory_order_relaxed);
    g_metrics_reference_rms.store(static_cast<uint32_t>(std::sqrt(static_cast<double>(reference_square) / sample_count)), std::memory_order_relaxed);
    g_metrics_clean_rms.store(static_cast<uint32_t>(std::sqrt(static_cast<double>(clean_square) / sample_count)), std::memory_order_relaxed);
    g_metrics_echo_correlation.store(delayed_correlation, std::memory_order_relaxed);
    g_metrics_reference_delay_samples.store(delay_samples, std::memory_order_relaxed);
    g_metrics_playback_active.store(g_playback_active.load(), std::memory_order_relaxed);
    g_metrics_sequence.fetch_add(1, std::memory_order_release);
    g_metrics_valid.store(true, std::memory_order_release);
}

[[maybe_unused]] static bool process_capture_audio(const int16_t *samples, size_t sample_count, int64_t capture_time_us) {
    if (!samples || sample_count == 0) return true;
    if (!g_aec) return feed_afe(samples, sample_count);
    if (g_aec_chunk <= 0 || static_cast<size_t>(g_aec_chunk) > kAecCapacity || g_aec_pending_count + sample_count > kAecCapacity) return false;
    std::memcpy(g_aec_pending + g_aec_pending_count, samples, sample_count * sizeof(int16_t));
    g_aec_pending_count += sample_count;
    if (!g_aec_input_frame || !g_aec_reference_frame || !g_aec_cleaned_frame) return false;
    while (g_aec_pending_count >= static_cast<size_t>(g_aec_chunk)) {
        std::memcpy(g_aec_input_frame, g_aec_pending, static_cast<size_t>(g_aec_chunk) * sizeof(int16_t));
        uint32_t reference_delay_samples = g_reference_delay_samples.load();
        constexpr size_t microphone_filter_delay = kMicrophoneRate == kCaptureRate ? 0 : kResampleHalf;
        const int64_t frame_start_us = capture_time_us - static_cast<int64_t>((static_cast<uint64_t>(g_aec_pending_count) * 1000000ULL) / kCaptureRate) - static_cast<int64_t>(microphone_filter_delay * 1000000ULL / kMicrophoneRate);
        const bool echo_expected = pop_reference(g_aec_reference_frame, static_cast<size_t>(g_aec_chunk), frame_start_us, &reference_delay_samples);
        std::memcpy(g_aec_original_mic, g_aec_input_frame, static_cast<size_t>(g_aec_chunk) * sizeof(int16_t));
        std::memcpy(g_aec_original_reference, g_aec_reference_frame, static_cast<size_t>(g_aec_chunk) * sizeof(int16_t));
        // 小智同板 Es8311AudioCodec 没有 input_reference，待机唤醒不经过 AEC。
        // 保留本项目播放与尾音期间的软件 AEC，无播放参考时直接使用原始麦克风。
        // https://github.com/78/xiaozhi-esp32/blob/v2.5.2/main/audio/engines/afe_audio_engine.cc
        if (echo_expected) aec_process(g_aec, g_aec_input_frame, g_aec_reference_frame, g_aec_cleaned_frame);
        else std::memcpy(g_aec_cleaned_frame, g_aec_input_frame, static_cast<size_t>(g_aec_chunk) * sizeof(int16_t));
        static bool previous_echo_expected = true;
        if (echo_expected != previous_echo_expected) {
            ESP_LOGI(kTag, "capture_frontend software_aec=%d reason=%s", echo_expected, echo_expected ? "playback_or_tail" : "no_playback_reference");
            previous_echo_expected = echo_expected;
        }
        uint64_t clean_energy = 0;
        uint64_t clean_square = 0;
        for (int index = 0; index < g_aec_chunk; ++index) {
            clean_energy += static_cast<uint16_t>(g_aec_cleaned_frame[index] < 0 ? -g_aec_cleaned_frame[index] : g_aec_cleaned_frame[index]);
            const int32_t value = g_aec_cleaned_frame[index];
            clean_square += static_cast<uint64_t>(value * value);
        }
        g_clean_energy.fetch_add(clean_energy);
        g_clean_square.fetch_add(clean_square);
        g_clean_samples.fetch_add(static_cast<size_t>(g_aec_chunk));
        publish_capture_metrics(g_aec_original_mic, g_aec_original_reference, g_aec_cleaned_frame, static_cast<size_t>(g_aec_chunk), reference_delay_samples);
        if (g_aec_chunk == static_cast<int>(kDiagnosticFrameSamples)) {
            diagnostic_capture_frame(g_aec_original_mic, g_aec_original_reference, g_aec_cleaned_frame);
        }
        if (!feed_afe(g_aec_cleaned_frame, static_cast<size_t>(g_aec_chunk))) return false;
        const size_t remaining = g_aec_pending_count - static_cast<size_t>(g_aec_chunk);
        std::memmove(g_aec_pending, g_aec_pending + g_aec_chunk, remaining * sizeof(int16_t));
        g_aec_pending_count = remaining;
    }
    return true;
}

static void log_audio_stats() {
    const int64_t now = esp_timer_get_time();
    if (g_last_stats_us != 0 && now - g_last_stats_us < 10000000) return;
    g_last_stats_us = now;
    const uint64_t mic_frames = g_mic_frames.exchange(0);
    const uint64_t reference_frames = g_reference_frames.exchange(0);
    const uint64_t output_frames = g_output_frames.exchange(0);
    const uint64_t reference_underflows = g_reference_underflows.exchange(0);
    const uint64_t reference_stale_drops = g_reference_stale_drops.exchange(0);
    const uint64_t capture_overflows = g_capture_overflows.exchange(0);
    const uint64_t mic_energy = g_mic_energy.exchange(0);
    const uint64_t reference_energy = g_reference_energy.exchange(0);
    const uint64_t clean_energy = g_clean_energy.exchange(0);
    const uint64_t mic_square = g_mic_square.exchange(0);
    const uint64_t reference_square = g_reference_square.exchange(0);
    const uint64_t clean_square = g_clean_square.exchange(0);
    const uint64_t mic_samples = g_mic_samples.exchange(0);
    const uint64_t reference_samples = g_reference_samples.exchange(0);
    const uint64_t clean_samples = g_clean_samples.exchange(0);
    const unsigned mic_rms = mic_samples ? static_cast<unsigned>(std::sqrt(static_cast<double>(mic_square) / mic_samples)) : 0;
    const unsigned reference_rms = reference_samples ? static_cast<unsigned>(std::sqrt(static_cast<double>(reference_square) / reference_samples)) : 0;
    const unsigned clean_rms = clean_samples ? static_cast<unsigned>(std::sqrt(static_cast<double>(clean_square) / clean_samples)) : 0;
    ESP_LOGI(kTag, "aec_stats enabled=%d reference=software_pcm24 sample_rate=16000 frame_ms=32 mic_frames=%llu reference_frames=%llu output_frames=%llu reference_underflows=%llu reference_stale_drops=%llu capture_overflows=%llu micRMS=%u refRMS=%u cleanRMS=%u mic_abs=%llu ref_abs=%llu clean_abs=%llu",
             g_aec_ready.load(), static_cast<unsigned long long>(mic_frames), static_cast<unsigned long long>(reference_frames), static_cast<unsigned long long>(output_frames), static_cast<unsigned long long>(reference_underflows), static_cast<unsigned long long>(reference_stale_drops), static_cast<unsigned long long>(capture_overflows), mic_rms, reference_rms, clean_rms, static_cast<unsigned long long>(mic_energy), static_cast<unsigned long long>(reference_energy), static_cast<unsigned long long>(clean_energy));
}

static void capture_producer_task(void *) {
    std::array<int16_t, kRawReadSamples> raw{};
    std::array<int16_t, 512> resampled{};
    int64_t gain_retry_after_us = 0;
    while (true) {
        // 在 processing 锁外恢复增益，避免 capture_start 的 state -> processing 锁顺序反转。
        if (microphone_gain_supported() && esp_timer_get_time() >= gain_retry_after_us && g_microphone_gain_db != kMicrophoneGainDb && !g_playback_active.load() && !g_playback_paused.load() &&
            esp_timer_get_time() - g_last_playback_start_us.load() > kReferenceStaleWindowUs &&
            g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
            if (!g_playback_active.load() && !g_playback_paused.load() && esp_timer_get_time() - g_last_playback_start_us.load() > kReferenceStaleWindowUs && g_codec_open.load()) {
                if (set_microphone_gain(kMicrophoneGainDb) == ESP_CODEC_DEV_OK) {
                    g_microphone_gain_db = kMicrophoneGainDb;
                    ESP_LOGI(kTag, "microphone_gain_db=%.0f reason=standby", static_cast<double>(kMicrophoneGainDb));
                } else {
                    gain_retry_after_us = esp_timer_get_time() + 5000000;
                    ESP_LOGE(kTag, "microphone_gain_restore_failed retry_after_ms=5000");
                }
            }
            xSemaphoreGive(g_state_lock);
        }
#if defined(ASSISTANT_BOARD_INTERNAL_RAM)
        if (g_afe_reset_pending.load() && xSemaphoreTake(g_capture_processing_lock, portMAX_DELAY) == pdTRUE) {
            if (g_afe_reset_pending.exchange(false)) {
                g_capture_resampler.reset();
                if (xSemaphoreTake(g_audio_buffer_lock, portMAX_DELAY) == pdTRUE) {
                    clear_afe_output_locked();
                    xSemaphoreGive(g_audio_buffer_lock);
                }
            }
            xSemaphoreGive(g_capture_processing_lock);
        }
#endif
        if (!g_capture_active.load() || !g_codec_open.load() || g_afe_reset_pending.load() || g_capture_failed.load()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (!acquire_capture_reader()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        const uint32_t capture_generation = g_afe_generation.load();
        const int result = esp_codec_dev_read(g_microphone, raw.data(), static_cast<int>(raw.size() * sizeof(int16_t)));
        const int64_t capture_time_us = esp_timer_get_time();
        release_capture_reader();
        if (!g_capture_active.load()) maybe_close_codec();
        if (result != ESP_CODEC_DEV_OK) {
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
            g_capture_failed = true;
            ESP_LOGE(kTag, "ADC capture stopped result=%d; owner restart required", result);
#endif
            if constexpr (board_profile::manual_audio_only) {
                if (capture_generation == g_afe_generation.load()) {
                    g_capture_failed = true;
                    ESP_LOGE(kTag, "manual_capture_read_failed result=%d; recording must be canceled", result);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        g_mic_frames.fetch_add(1);
        uint64_t mic_square = 0;
        uint64_t mic_energy = 0;
        for (const int16_t sample : raw) {
            mic_energy += static_cast<uint16_t>(sample < 0 ? -sample : sample);
            const int32_t value = sample;
            mic_square += static_cast<uint64_t>(value * value);
        }
        g_mic_energy.fetch_add(mic_energy);
        g_mic_square.fetch_add(mic_square);
        g_mic_samples.fetch_add(raw.size());
        if (g_capture_processing_lock && xSemaphoreTake(g_capture_processing_lock, portMAX_DELAY) != pdTRUE) continue;
        if (!g_capture_active.load() || (board_profile::manual_audio_only &&
            (capture_generation != g_afe_generation.load() || g_afe_reset_pending.load()))) {
            if (g_capture_processing_lock) xSemaphoreGive(g_capture_processing_lock);
            continue;
        }
        if (!g_capture_resampler.append(raw.data(), raw.size())) {
            if constexpr (board_profile::manual_audio_only) g_capture_failed = true;
            if (g_capture_processing_lock) xSemaphoreGive(g_capture_processing_lock);
            continue;
        }
        const size_t output_count = g_capture_resampler.produce(resampled.data(), resampled.size());
#if defined(ASSISTANT_BOARD_INTERNAL_RAM)
        // Preserve generation/sample-index/discontinuity metadata without
        // inventing a VAD label. A reset during the read invalidates this PCM.
        const bool processed = capture_generation == g_afe_generation.load() && !g_afe_reset_pending.load();
        if (processed) push_afe_output(resampled.data(), output_count, 0, capture_generation);
        (void)capture_time_us;
#else
        (void)capture_generation;
        const bool processed = process_capture_audio(resampled.data(), output_count, capture_time_us);
#endif
        if (g_capture_processing_lock) xSemaphoreGive(g_capture_processing_lock);
        if (!processed) continue;
        log_audio_stats();
    }
}

}  // namespace

esp_err_t audio_io_init() {
    if (g_initialized.load()) return ESP_OK;
#if defined(ASSISTANT_BOARD_OTTO)
    if (!otto_board_ready()) return ESP_ERR_INVALID_STATE;
    kCodecRate = board_profile::speaker_sample_rate;
    if (kCodecRate != 16000 && kCodecRate != 24000) return ESP_ERR_NOT_SUPPORTED;
#endif
    g_state_lock = xSemaphoreCreateMutex();
    g_playback_lock = xSemaphoreCreateMutex();
    g_audio_buffer_lock = xSemaphoreCreateMutex();
    g_reference_lock = xSemaphoreCreateMutex();
    g_capture_processing_lock = xSemaphoreCreateMutex();
    if (!g_state_lock || !g_playback_lock || !g_audio_buffer_lock || !g_reference_lock || !g_capture_processing_lock) return ESP_ERR_NO_MEM;
#if defined(ASSISTANT_BOARD_SENSAIR_C5)
    ESP_RETURN_ON_ERROR(sensair_audio_init(&g_codec, &g_microphone), kTag, "ADC/PDM audio");
#elif (defined(ASSISTANT_BOARD_AMOLED_18) && !defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_LC_TFT) || defined(ASSISTANT_BOARD_XINGZHI_ABS) || defined(ASSISTANT_BOARD_XINGZHI_METAL) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_GENJUTECH) || ((defined(ASSISTANT_BOARD_MAZA) && !defined(ASSISTANT_BOARD_MAZA_S31)) || ((defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_WS_EPAPER397)) || defined(ASSISTANT_BOARD_ZECTRIX))) || defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH)) || (defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_SPARKBOT)))) || defined(ASSISTANT_BOARD_P4_FUNCTION) || defined(ASSISTANT_BOARD_P4_SPI35)
#if defined(ASSISTANT_BOARD_ATK_BOX)
    if (atk_box_audio() == AtkBoxAudio::Uninitialized) return ESP_ERR_INVALID_STATE;
    if (atk_box_audio() == AtkBoxAudio::Digital) {
        ESP_RETURN_ON_ERROR(digital_audio_init(&g_codec, &g_microphone), kTag, "ATK BOX digital duplex");
    } else {
#endif
    if (!bsp_i2c_get_handle()) return ESP_ERR_INVALID_STATE;
#if (defined(ASSISTANT_BOARD_LC_TFT) || defined(ASSISTANT_BOARD_XINGZHI_ABS) || defined(ASSISTANT_BOARD_XINGZHI_METAL) || defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_GENJUTECH) || ((defined(ASSISTANT_BOARD_MAZA) && !defined(ASSISTANT_BOARD_MAZA_S31)) || ((defined(ASSISTANT_BOARD_WS_EPAPER) || defined(ASSISTANT_BOARD_WS_EPAPER397)) || defined(ASSISTANT_BOARD_ZECTRIX))) || defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_MAGICLICK) || defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KEVIN_RGB) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_LCDWIKI) || (defined(ASSISTANT_BOARD_QUANDONG) || (defined(ASSISTANT_BOARD_WS35) || defined(ASSISTANT_BOARD_STOPWATCH)) || (defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_SPARKBOT)))) || defined(ASSISTANT_BOARD_P4_FUNCTION) || defined(ASSISTANT_BOARD_P4_SPI35)
    constexpr i2s_port_t codec_port = I2S_NUM_0;
#else
    constexpr i2s_port_t codec_port = static_cast<i2s_port_t>(CONFIG_BSP_I2S_NUM);
#endif
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(codec_port, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &g_tx, &g_rx), kTag, "I2S channel");
#if defined(ASSISTANT_BOARD_AMOLED_132)
    const gpio_num_t word_select = BSP_I2S_WS;
#else
    const gpio_num_t word_select = BSP_I2S_LCLK;
#endif
    i2s_std_config_t i2s_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kCodecRate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = word_select, .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN,
            .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
    };
#if defined(ASSISTANT_BOARD_AIVOX3)
    // The pinned AIVOX3 codec starts with stereo16 and a128fs MCLK.
    i2s_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_128;
    i2s_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#endif
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_tx, &i2s_cfg), kTag, "I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_rx, &i2s_cfg), kTag, "I2S RX");
    audio_codec_i2s_cfg_t data_cfg = {};
    data_cfg.port = codec_port;
    data_cfg.rx_handle = g_rx;
    data_cfg.tx_handle = g_tx;
    g_data_if = audio_codec_new_i2s_data(&data_cfg);
    g_gpio_if = audio_codec_new_gpio();
    if (!g_data_if || !g_gpio_if) return ESP_ERR_NO_MEM;
    audio_codec_i2c_cfg_t ctrl_cfg = {.port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bsp_i2c_get_handle()};
    g_ctrl_if = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    if (!g_ctrl_if) return ESP_ERR_NO_MEM;
    esp_codec_dev_hw_gain_t gain = {};
#if defined(ASSISTANT_BOARD_WS_EPAPER397) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE))
    // These boards power the amplifier and ES8311 from3.3V.
    // Atomic Echo Base v1.0 powers both NS4150B and ES8311 from3.3V.
    gain.pa_voltage = 3.3;
#else
    gain.pa_voltage = 5.0;
#endif
    gain.codec_dac_voltage = 3.3;
    es8311_codec_cfg_t es_cfg = {};
    es_cfg.ctrl_if = g_ctrl_if;
    es_cfg.gpio_if = g_gpio_if;
    es_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
#if defined(ASSISTANT_BOARD_GENJUTECH)
    // The pinned board cites a display/PA conflict. board_power holds PA21;
    // codec close must not pull it low. ES8311 register mute/volume still apply.
    es_cfg.pa_pin = GPIO_NUM_NC;
#else
    es_cfg.pa_pin = BSP_POWER_AMP_IO;
#endif
#if defined(ASSISTANT_BOARD_STICK_S3) || defined(ASSISTANT_BOARD_ATOM_ECHOS3R) || defined(ASSISTANT_BOARD_ATOM_M12_BASE) || (defined(ASSISTANT_BOARD_ATOM_S3R_BASE) || defined(ASSISTANT_BOARD_ATOM_S3_BASE)) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_CARDPUTER_ADV) || defined(ASSISTANT_BOARD_STOPWATCH)
    // The pinned board constructor explicitly selects BCLK-derived codec clock.
    es_cfg.use_mclk = false;
#else
    es_cfg.use_mclk = true;
#endif
#if defined(ASSISTANT_BOARD_AIVOX3)
    es_cfg.mclk_div = I2S_MCLK_MULTIPLE_128;
#endif
    es_cfg.hw_gain = gain;
    g_codec_if = es8311_codec_new(&es_cfg);
    if (!g_codec_if) return ESP_ERR_NO_MEM;
    esp_codec_dev_cfg_t dev_cfg = {.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = g_codec_if, .data_if = g_data_if};
    g_codec = esp_codec_dev_new(&dev_cfg);
    if (!g_codec) return ESP_ERR_NO_MEM;
    g_microphone = g_codec;
#if defined(ASSISTANT_BOARD_GENJUTECH) || defined(ASSISTANT_BOARD_AIVOX3) || defined(ASSISTANT_BOARD_SPARKBOT)
    if (esp_codec_set_disable_when_closed(g_codec, false) != ESP_CODEC_DEV_OK) return ESP_FAIL;
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX)
    }
#endif
#elif defined(ASSISTANT_BOARD_TCIRCLE) || (defined(ASSISTANT_BOARD_TAIJI) || (defined(ASSISTANT_BOARD_TCAMERA) || (defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_K10)))) || ((defined(ASSISTANT_BOARD_HEADLESS) && !defined(ASSISTANT_BOARD_SPOT_C5)) && !defined(ASSISTANT_BOARD_ATOM_ECHOS3R) && !defined(ASSISTANT_BOARD_ATOM_M12_BASE) && !((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31))) || defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185) || defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_GUITION) || (defined(ASSISTANT_BOARD_OLED) && !defined(ASSISTANT_BOARD_KEVIN_BOX2))
    ESP_RETURN_ON_ERROR(digital_audio_init(&g_codec, &g_microphone), kTag, "digital board audio");
#elif (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_WAVESHARE_SPI) || defined(ASSISTANT_BOARD_LCD_185B) || defined(ASSISTANT_BOARD_LCD_185C) || defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || ((defined(ASSISTANT_BOARD_WS_RLCD) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C))) || (defined(ASSISTANT_BOARD_MATRIX) || defined(ASSISTANT_BOARD_SENSECAP)))) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)) || defined(ASSISTANT_BOARD_S31_KORVO) || defined(ASSISTANT_BOARD_CORES3)
    ESP_RETURN_ON_ERROR(waveshare_spi_audio_init(&g_codec, &g_microphone), kTag, "SPI board audio");
#else
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), kTag, "board I2C");
    ESP_RETURN_ON_ERROR(bsp_audio_init(nullptr), kTag, "board I2S");
    g_codec = bsp_audio_codec_speaker_init();
    g_microphone = bsp_audio_codec_microphone_init();
    if (!g_codec || !g_microphone) return ESP_ERR_NO_MEM;
#if defined(ASSISTANT_BOARD_CORES3)
    ESP_LOGI(kTag, "board_audio board=%s input=ES7210 output=AW88298 selected_mic=1 reference=software_pcm24", board_profile::name);
#elif defined(ASSISTANT_BOARD_BOXLITE)
    ESP_LOGI(kTag, "board_audio board=%s input=ES7243E output=ES8156 selected_mic=1 reference=software_pcm24", board_profile::name);
#elif defined(ASSISTANT_BOARD_WS_RGB_7C)
    ESP_LOGI(kTag, "board_audio board=%s input=ES7210 output=ES8389 stereo=duplicated_mono selected_mic=1 reference=software_pcm24", board_profile::name);
#else
    ESP_LOGI(kTag, "board_audio board=%s input=ES7210 output=ES8311 selected_mic=1 reference=software_pcm24", board_profile::name);
#endif
#endif
#if !defined(ASSISTANT_BOARD_INTERNAL_RAM)
    // esp_srmodel_init returns the component's shared static model list.
#if defined(ASSISTANT_BOARD_GUITION)
    // Single-app 4MB profiles have no model partition. ESP-SR's public AFE
    // contract uses WebRTC VAD when vad_model_name is null.
    g_models = nullptr;
#else
    g_models = esp_srmodel_init("model");
#endif
    // Keep a mono FD front end; new dual-mic boards select MIC1 above and use
    // the same software playback reference until acoustic acceptance is done.
#if CONFIG_IDF_TARGET_ESP32C5
    afe_config_t *afe_config = afe_config_init("M", g_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
#else
    afe_config_t *afe_config = afe_config_init("M", g_models, AFE_TYPE_FD, AFE_MODE_LOW_COST);
#endif
    if (!afe_config) return ESP_ERR_NO_MEM;
    afe_config->aec_init = false;
    afe_config->se_init = false;
    // NS 实测代价：MN 推理 15ms→43-76ms，超过 32ms 帧周期，播放期唤醒检测
    // 完全失效（0.2.6-0.2.8 教训）。CPU 实时性优先，NS 关闭；残留回声由
    // AEC VERYAGGR 抑制（echo_floor 6179→1195，保留）。
    afe_config->ns_init = false;
    afe_config->vad_init = true;
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_noise_ms = 100;
    afe_config->vad_model_name = g_models ? esp_srmodel_filter(g_models, ESP_VADN_PREFIX, nullptr) : nullptr;
    afe_config->wakenet_init = false;
    afe_config->wakenet_model_name = nullptr;
    afe_config->wakenet_model_name_2 = nullptr;
    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
#if CONFIG_IDF_TARGET_ESP32C5
    afe_config->afe_perferred_core = 0;
#endif
    g_afe_iface = esp_afe_handle_from_config(afe_config);
    if (g_afe_iface) g_afe_data = g_afe_iface->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (!g_afe_iface || !g_afe_data) {
        ESP_LOGE(kTag, "AFE initialization failed");
        return ESP_FAIL;
    }
#if CONFIG_IDF_TARGET_ESP32C5
    // C5 exposes the SR AEC API, not S3's FD/NLP interface. Retain the
    // existing software reference and32ms frame path with its supported mode.
    g_aec = aec_create(kCaptureRate, 2, 1, AEC_MODE_SR_LOW_COST);
#else
    g_aec = aec_create(kCaptureRate, 4, 1, AEC_MODE_FD_LOW_COST);
#endif
    if (g_aec) {
        // 对齐小智 VERYAGGR：单麦软件参考的残留回声是自然插话误判的根源，
        // 最强非线性抑制直接压低 echo_floor；代价由插话误判率下降抵偿。
#if !CONFIG_IDF_TARGET_ESP32C5
        aec_set_nlp_level(g_aec, AEC_NLP_LEVEL_VERYAGGR);
#endif
        g_aec_chunk = aec_get_chunksize(g_aec);
        g_aec_ready = g_aec_chunk > 0 && static_cast<size_t>(g_aec_chunk) <= kAecCapacity;
        if (!g_aec_ready.load()) {
            aec_destroy(g_aec);
            g_aec = nullptr;
        }
        if (g_aec_ready.load()) {
            const size_t frame_bytes = static_cast<size_t>(g_aec_chunk) * sizeof(int16_t);
            g_aec_input_frame = static_cast<int16_t *>(heap_caps_aligned_alloc(16, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            g_aec_reference_frame = static_cast<int16_t *>(heap_caps_aligned_alloc(16, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            g_aec_cleaned_frame = static_cast<int16_t *>(heap_caps_aligned_alloc(16, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!g_aec_input_frame || !g_aec_reference_frame || !g_aec_cleaned_frame) {
                ESP_LOGW(kTag, "Software AEC frame buffers unavailable");
                g_aec_ready = false;
                if (g_aec_input_frame) heap_caps_free(g_aec_input_frame);
                if (g_aec_reference_frame) heap_caps_free(g_aec_reference_frame);
                if (g_aec_cleaned_frame) heap_caps_free(g_aec_cleaned_frame);
                g_aec_input_frame = nullptr;
                g_aec_reference_frame = nullptr;
                g_aec_cleaned_frame = nullptr;
                aec_destroy(g_aec);
                g_aec = nullptr;
            }
        }
    } else {
        ESP_LOGW(kTag, "Software AEC initialization failed; capture remains AFE processed");
    }
#endif
#if CONFIG_IDF_TARGET_ESP32C5
    // Do not silently start a reduced capture path if C5 AEC could not start.
    if (!g_aec_ready.load()) return ESP_ERR_NO_MEM;
#endif
    g_capture_resampler.reset();
    g_reference_resampler.reset();
    g_capture_resampler.initialize_coefficients();
    g_reference_resampler.initialize_coefficients();
#if !defined(ASSISTANT_BOARD_INTERNAL_RAM)
    if (xTaskCreateWithCaps(afe_task, "audio_afe", 8192, nullptr, 4, nullptr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) return ESP_ERR_NO_MEM;
#endif
    if (xTaskCreateWithCaps(capture_producer_task, "capture_producer", 6144, nullptr, 5, nullptr, board_profile::working_memory_caps) != pdPASS) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(g_state_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = codec_open_locked(kCodecRate);
    xSemaphoreGive(g_state_lock);
    if (result != ESP_OK) return result;
    g_afe_reset_pending = true;
    g_capture_active = true;
    g_initialized = true;
    ESP_LOGI(kTag, "hardware_audio microphone_rate=%lu speaker_rate=%lu network_pcm_rate=24000", static_cast<unsigned long>(kMicrophoneRate), static_cast<unsigned long>(kCodecRate));
#if defined(ASSISTANT_BOARD_INTERNAL_RAM)
    ESP_LOGI(kTag, "manual_capture_ready FIR16kHz no_AFE no_AEC no_local_wake output_ring_samples=%u", static_cast<unsigned>(kAfeOutputCapacity));
#else
    ESP_LOGI(kTag, "AFE ready feed=%d fetch=%d software_aec=%d", g_afe_iface->get_feed_chunksize(g_afe_data), g_afe_iface->get_fetch_chunksize(g_afe_data), g_aec_ready.load());
#endif
    return ESP_OK;
}

esp_err_t audio_capture_start() {
    if (!g_initialized.load() || !g_state_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_state_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = codec_open_locked(kCodecRate);
    if (result == ESP_OK) {
        clear_audio_buffers();
        g_afe_generation.fetch_add(1);
        g_afe_reset_pending = true;
        g_capture_active = true;
        g_metrics_valid = false;
    }
    xSemaphoreGive(g_state_lock);
    return result;
}

void audio_capture_stop() {
    if (g_playback_lock && xSemaphoreTake(g_playback_lock, portMAX_DELAY) != pdTRUE) return;
    g_capture_active = false;
    g_vad_voice_active = false;
    g_metrics_valid = false;
    g_afe_generation.fetch_add(1);
    g_afe_reset_pending = true;
    clear_audio_buffers();
    g_reference_generation.fetch_add(1);
    clear_reference();
    if (g_playback_lock) xSemaphoreGive(g_playback_lock);
    maybe_close_codec();
}

void audio_capture_discard_pending() {
    // 先使在途 AFE fetch 失效，再清 PCM/VAD 和前端积压，不能只清最终输出环。
    g_afe_generation.fetch_add(1);
    g_afe_reset_pending = true;
    clear_audio_buffers();
    g_metrics_valid = false;
}

bool audio_capture_failed() { return g_capture_failed.load(); }

bool audio_capture_pending(AudioCapturePending *pending) {
    if (!pending) return false;
    *pending = {};
    if (!g_audio_buffer_lock || xSemaphoreTake(g_audio_buffer_lock, portMAX_DELAY) != pdTRUE) return false;
    const bool valid = g_afe_output_capture_generation == g_afe_generation.load() && !g_afe_reset_pending.load() && g_capture_active.load();
    if (valid) {
        // 仅快照已进入输出环的 PCM，不包含 AFE 内尚未 fetch 的数据。
        pending->generation = g_afe_output_generation;
        pending->sample_index = g_afe_output_next_sample - g_afe_output_count;
        pending->bytes = g_afe_output_count * sizeof(int16_t);
    }
    xSemaphoreGive(g_audio_buffer_lock);
    return valid;
}

size_t audio_capture_read(uint8_t *buffer, size_t capacity, AudioCaptureFrame *frame) {
    if (frame) *frame = {};
    if (!buffer || capacity < sizeof(int16_t) || !g_capture_active.load() || g_afe_reset_pending.load() || g_capture_failed.load()) return 0;
    const size_t requested_samples = std::min(capacity / sizeof(int16_t), kMaxReadSamples);
    int16_t *output = reinterpret_cast<int16_t *>(buffer);
    size_t output_count = pop_afe_output(output, requested_samples, frame);
    if (output_count == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
        output_count = pop_afe_output(output, requested_samples, frame);
    }
    g_output_frames.fetch_add(output_count);
    return output_count * sizeof(int16_t);
}

esp_err_t audio_prepare_pcm24() {
    if (!g_initialized.load() || !g_playback_lock || !g_state_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_playback_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) { xSemaphoreGive(g_playback_lock); return ESP_ERR_TIMEOUT; }
    const esp_err_t result = codec_open_locked(kCodecRate);
    if (result == ESP_OK) g_playback_paused.store(false);
    xSemaphoreGive(g_state_lock);
    xSemaphoreGive(g_playback_lock);
    return result;
}

esp_err_t audio_play_pcm24(uint8_t *data, size_t length) {
    if (!g_initialized.load() || !data || length == 0 || (length % sizeof(int16_t)) != 0) return ESP_ERR_INVALID_ARG;
    const uint32_t generation = g_reference_generation.load();
    if (!g_playback_lock || xSemaphoreTake(g_playback_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t open_result = ESP_OK;
    bool playback_marked = false;
    bool playback_silent = false;
    if (generation != g_reference_generation.load() || !g_state_lock || xSemaphoreTake(g_state_lock, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(g_playback_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (generation != g_reference_generation.load()) {
        xSemaphoreGive(g_state_lock);
        xSemaphoreGive(g_playback_lock);
        return ESP_ERR_INVALID_STATE;
    }
    open_result = codec_open_locked(kCodecRate);
    if (open_result == ESP_OK) {
        if ((g_output_settings_dirty.load() || g_applied_notification_output != g_notification_output.load() ||
             g_output_muted.load() != requested_output_mute(g_applied_notification_output)) && apply_output_settings_locked() != ESP_OK) {
            xSemaphoreGive(g_state_lock);
            xSemaphoreGive(g_playback_lock);
            return ESP_FAIL;
        }
        playback_silent = g_output_muted.load() || (g_applied_notification_output ? g_reminder_volume.load() : g_conversation_volume.load()) == 0;
        if (microphone_gain_supported() && g_microphone_gain_db != kPlaybackMicrophoneGainDb) {
            if (set_microphone_gain(kPlaybackMicrophoneGainDb) != ESP_CODEC_DEV_OK) {
                xSemaphoreGive(g_state_lock);
                xSemaphoreGive(g_playback_lock);
                return ESP_FAIL;
            }
            g_microphone_gain_db = kPlaybackMicrophoneGainDb;
            ESP_LOGI(kTag, "microphone_gain_db=12 reason=playback");
        }
        g_playback_active = true;
        g_last_playback_start_us = esp_timer_get_time();
        playback_marked = true;
    }
    xSemaphoreGive(g_state_lock);
    if (open_result != ESP_OK) {
        xSemaphoreGive(g_playback_lock);
        return open_result;
    }

    // esp_codec_dev1.5.11 can return OK without forwarding the codec mute
    // callback's failure. Send silent PCM as well, including analog boards.
    // Feeding silence into the FIR also avoids retaining muted source audio.
    if (playback_silent) std::memset(data, 0, length);

#if defined(ASSISTANT_BOARD_INTERNAL_RAM)
    static_assert(board_profile::speaker_sample_rate == 24000, "Manual internal-RAM output currently requires native24kHz");
    const int write_result = esp_codec_dev_write(g_codec, data, static_cast<int>(length));
#else
    // g_playback_lock 串行保护，避免把 2KB 参考缓冲压在播放任务栈上。
    EXT_RAM_BSS_ATTR static std::array<int16_t, 1024> reference;
    size_t reference_count = 0;
    bool reference_ready = false;
    if (generation == g_reference_generation.load() && g_reference_lock && xSemaphoreTake(g_reference_lock, portMAX_DELAY) == pdTRUE) {
        reference_ready = g_reference_resampler.append(reinterpret_cast<const int16_t *>(data), length / sizeof(int16_t));
        if (reference_ready) reference_count = g_reference_resampler.produce(reference.data(), reference.size());
        xSemaphoreGive(g_reference_lock);
    }
    if (!reference_ready || generation != g_reference_generation.load()) {
        if (playback_marked && g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
            g_playback_active = false;
            xSemaphoreGive(g_state_lock);
        }
        clear_reference();
        xSemaphoreGive(g_playback_lock);
        maybe_close_codec();
        return ESP_ERR_INVALID_STATE;
    }
    // Preference/channel changes also acquire g_playback_lock. Keep one mute
    // snapshot for this packet's physical output and software AEC reference.
    if (reference_count && !push_reference(reference.data(), reference_count, playback_silent)) {
        g_reference_generation.fetch_add(1);
        clear_reference();
        if (playback_marked && g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
            g_playback_active = false;
            xSemaphoreGive(g_state_lock);
        }
        xSemaphoreGive(g_playback_lock);
        maybe_close_codec();
        return ESP_ERR_NO_MEM;
    }
    int write_result = ESP_CODEC_DEV_OK;
#if defined(ASSISTANT_BOARD_OTTO)
    if (kCodecRate == 16000) {
#else
    if constexpr (kCodecRate == 16000) {
#endif
        // The existing24-to16 FIR yields the exact samples sent to both the
        // native speaker and AEC. Its history is reset with playback abort.
        // The filter can still hold a short tail from before muting.
        if (playback_silent) std::memset(reference.data(), 0, reference_count * sizeof(int16_t));
        if (reference_count) write_result = esp_codec_dev_write(g_codec, reference.data(), static_cast<int>(reference_count * sizeof(int16_t)));
    } else {
#if defined(ASSISTANT_BOARD_WS_RGB_7C) || defined(ASSISTANT_BOARD_S31_KORVO)
        write_result = waveshare_spi_audio_write_mono(g_codec, data, length);
#else
        write_result = esp_codec_dev_write(g_codec, data, static_cast<int>(length));
#endif
    }
#endif
    if (write_result != ESP_CODEC_DEV_OK) {
        g_reference_generation.fetch_add(1);
        clear_reference();
    }
    if (playback_marked && g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
        g_playback_active = false;
        xSemaphoreGive(g_state_lock);
    }
    xSemaphoreGive(g_playback_lock);
    maybe_close_codec();
    return write_result == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

void audio_playback_abort() {
    if (!g_playback_lock || xSemaphoreTake(g_playback_lock, portMAX_DELAY) != pdTRUE) return;
    // 中断回待机不能继承候选暂停标记，否则待机增益会一直无法恢复。
    g_playback_paused.store(false);
    g_output_settings_dirty = true;
    if (g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
        g_reference_generation.fetch_add(1);
        if (g_codec && g_open) {
            const int result = set_output_mute(true);
            if (result == ESP_CODEC_DEV_OK) g_output_muted = true;
            else ESP_LOGE(kTag, "playback_abort_mute_failed result=%d", result);
        }
        g_playback_active = false;
        xSemaphoreGive(g_state_lock);
    } else {
        g_reference_generation.fetch_add(1);
        g_playback_active = false;
    }
    clear_reference();
    xSemaphoreGive(g_playback_lock);
    maybe_close_codec();
}

esp_err_t audio_playback_pause(bool paused) {
    if (!g_initialized.load() || !g_playback_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_playback_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    g_playback_paused.store(paused);
    g_output_settings_dirty = true;
    esp_err_t result = ESP_OK;
    if (g_state_lock && xSemaphoreTake(g_state_lock, portMAX_DELAY) == pdTRUE) {
        result = apply_output_settings_locked();
        // 暂停只在插话候选期发生：播放已静音无削顶风险，收音提回30dB让近端确认灵敏；
        // 恢复播放前降回12dB防播放时ADC削顶。
        if (microphone_gain_supported() && g_codec && g_open) {
            if (set_microphone_gain(paused ? kMicrophoneGainDb : kPlaybackMicrophoneGainDb) == ESP_CODEC_DEV_OK) {
                g_microphone_gain_db = paused ? kMicrophoneGainDb : kPlaybackMicrophoneGainDb;
                ESP_LOGI(kTag, "microphone_gain_db=%.0f reason=%s", static_cast<double>(g_microphone_gain_db.load()), paused ? "candidate" : "resume");
            }
        }
        xSemaphoreGive(g_state_lock);
    } else {
        result = ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(g_playback_lock);
    return result;
}

esp_err_t audio_set_output_preferences(uint8_t conversation_volume, uint8_t reminder_volume, bool conversation_muted, bool reminders_muted) {
    if (conversation_volume > 100 || reminder_volume > 100) return ESP_ERR_INVALID_ARG;
    if (!g_state_lock || !g_playback_lock) {
        // Retain pre-initialization preferences for the first codec open.
        g_conversation_volume = conversation_volume; g_reminder_volume = reminder_volume;
        g_conversation_muted = conversation_muted; g_reminders_muted = reminders_muted;
        g_output_settings_dirty = true;
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(g_playback_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) { xSemaphoreGive(g_playback_lock); return ESP_ERR_TIMEOUT; }
    g_conversation_volume = conversation_volume; g_reminder_volume = reminder_volume;
    g_conversation_muted = conversation_muted; g_reminders_muted = reminders_muted;
    const esp_err_t result = apply_output_settings_locked();
    xSemaphoreGive(g_state_lock);
    xSemaphoreGive(g_playback_lock);
    return result;
}

esp_err_t audio_adjust_conversation_volume(int delta, uint8_t *new_volume) {
    if (!new_volume || delta < -100 || delta > 100) return ESP_ERR_INVALID_ARG;
    if (!g_initialized.load() || !g_state_lock || !g_playback_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_playback_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) { xSemaphoreGive(g_playback_lock); return ESP_ERR_TIMEOUT; }
    const int volume = std::max(0, std::min(100, g_conversation_volume.load() + delta));
    esp_err_t result = ESP_OK;
    // A local key changes conversation volume only, including when a reminder
    // is active or a notification-channel request is waiting to be applied.
    if (g_codec && g_open && !g_notification_output.load() && !g_applied_notification_output) {
        const int volume_result = set_output_volume(volume);
        if (volume_result != ESP_CODEC_DEV_OK) {
            g_output_settings_dirty = true;
            ESP_LOGE(kTag, "local_volume_failed result=%d", volume_result);
            result = ESP_FAIL;
        }
    }
    if (result == ESP_OK) {
        g_conversation_volume.store(volume);
        *new_volume = static_cast<uint8_t>(volume);
    }
    xSemaphoreGive(g_state_lock);
    xSemaphoreGive(g_playback_lock);
    return result;
}

esp_err_t audio_set_notification_output(bool notification) {
    // Preserve intent even on a lock timeout. An in-flight packet uses its
    // confirmed channel; the next packet retries settings before writing PCM.
    g_notification_output.store(notification);
    g_output_settings_dirty = true;
    if (!g_state_lock || !g_playback_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_playback_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(g_state_lock, pdMS_TO_TICKS(1000)) != pdTRUE) { xSemaphoreGive(g_playback_lock); return ESP_ERR_TIMEOUT; }
    const esp_err_t result = apply_output_settings_locked();
    xSemaphoreGive(g_state_lock);
    xSemaphoreGive(g_playback_lock);
    return result;
}

bool audio_echo_cancellation_ready() { return g_initialized.load() && g_aec_ready.load(); }
bool audio_capture_voice_active() { return g_vad_voice_active.load(); }

bool audio_capture_metrics(AudioCaptureMetrics *metrics) {
    if (!metrics || !g_metrics_valid.load(std::memory_order_acquire)) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint32_t sequence_before = g_metrics_sequence.load(std::memory_order_acquire);
        if (sequence_before & 1U) continue;
        AudioCaptureMetrics snapshot{};
        snapshot.voice_active = g_metrics_voice_active.load(std::memory_order_relaxed);
        snapshot.mic_rms = g_metrics_mic_rms.load(std::memory_order_relaxed);
        snapshot.reference_rms = g_metrics_reference_rms.load(std::memory_order_relaxed);
        snapshot.clean_rms = g_metrics_clean_rms.load(std::memory_order_relaxed);
        snapshot.echo_correlation = g_metrics_echo_correlation.load(std::memory_order_relaxed);
        snapshot.reference_delay_samples = g_metrics_reference_delay_samples.load(std::memory_order_relaxed);
        snapshot.playback_active = g_metrics_playback_active.load(std::memory_order_relaxed);
        const uint32_t sequence_after = g_metrics_sequence.load(std::memory_order_acquire);
        if (sequence_before == sequence_after) {
            *metrics = snapshot;
            return true;
        }
    }
    return false;
}

esp_err_t audio_diagnostic_start(uint32_t frame_count) {
    if constexpr (board_profile::manual_audio_only) return ESP_ERR_NOT_SUPPORTED;
    if (frame_count == 0 || frame_count > kDiagnosticMaxFrames) return ESP_ERR_INVALID_ARG;
    if (!g_initialized.load() || !g_capture_active.load() || !g_aec_ready.load() ||
        g_aec_chunk != static_cast<int>(kDiagnosticFrameSamples) || !g_capture_processing_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(g_capture_processing_lock, portMAX_DELAY) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_OK;
    if (g_diagnostic_state.load(std::memory_order_acquire) != DiagnosticState::Inactive) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        const size_t bytes = static_cast<size_t>(frame_count) * kDiagnosticFrameSamples * kDiagnosticChannels * sizeof(int16_t);
        g_diagnostic_buffer = static_cast<int16_t *>(heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!g_diagnostic_buffer) {
            result = ESP_ERR_NO_MEM;
        } else {
            g_diagnostic_frame_count = frame_count;
            g_diagnostic_frames_written = 0;
            g_diagnostic_bytes = bytes;
            g_diagnostic_read_offset = 0;
            g_diagnostic_state.store(DiagnosticState::Capturing, std::memory_order_release);
        }
    }
    xSemaphoreGive(g_capture_processing_lock);
    return result;
}

size_t audio_diagnostic_read(uint8_t *dst, size_t capacity) {
    if (!dst || capacity == 0 || !g_capture_processing_lock) return 0;
    if (xSemaphoreTake(g_capture_processing_lock, portMAX_DELAY) != pdTRUE) return 0;
    DiagnosticState state = g_diagnostic_state.load(std::memory_order_acquire);
    if (state == DiagnosticState::Ready) {
        g_diagnostic_state.store(DiagnosticState::Reading, std::memory_order_release);
        state = DiagnosticState::Reading;
    }
    size_t copied = 0;
    if (state == DiagnosticState::Reading && g_diagnostic_buffer && g_diagnostic_read_offset < g_diagnostic_bytes) {
        copied = std::min(capacity, g_diagnostic_bytes - g_diagnostic_read_offset);
        std::memcpy(dst, reinterpret_cast<const uint8_t *>(g_diagnostic_buffer) + g_diagnostic_read_offset, copied);
        g_diagnostic_read_offset += copied;
        if (g_diagnostic_read_offset == g_diagnostic_bytes) diagnostic_discard_locked();
    }
    xSemaphoreGive(g_capture_processing_lock);
    return copied;
}

bool audio_diagnostic_ready() {
    const auto state = g_diagnostic_state.load(std::memory_order_acquire);
    return state == DiagnosticState::Ready || state == DiagnosticState::Reading;
}
