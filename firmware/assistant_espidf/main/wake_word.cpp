#include "wake_word.h"
#include <math.h>
#include <string.h>
#include <atomic>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#if defined(ASSISTANT_WAKE_PCM_PROBE)
#include "wake_probe_samples.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "wake";
static constexpr char kWakePhrase[] = "ni hao yi di si";
// mn6_cn 识别率高于量化 mn5q8；模型分区烧录 srmodels-mn6_cn-sr247.bin 后自动生效，
// 未烧录时回退 mn5q8_cn，行为与历史版本一致。
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
// ESP-SR2.4.7 exposes Chinese mn7 on P4/S31; this port requires that model.
static constexpr char kPreferredModelName[] = "mn7_cn";
#else
static constexpr char kPreferredModelName[] = "mn6_cn";
#endif
static constexpr char kFallbackModelName[] = "mn5q8_cn";
static constexpr int kModelDurationMs = 3000;
static constexpr float kDetectionThreshold = 0.20f;
static std::atomic<float> g_threshold{kDetectionThreshold};
static constexpr size_t kPendingCapacity = 2048;
static constexpr size_t kProbeFrameSamples = 320;
static const esp_mn_iface_t *g_iface;
static model_iface_data_t *g_model;
static int g_chunk;
static int16_t g_pending[kPendingCapacity];
static size_t g_pending_count;
static srmodel_list_t *g_models;
static char *g_model_name;
static uint64_t g_detect_total_us;
static uint32_t g_detect_calls;
static uint32_t g_detect_max_us;
static uint64_t g_input_square;
static uint32_t g_input_samples;
static uint32_t g_input_peak;
static uint32_t g_input_frames;
static uint32_t g_pending_resets;
#if defined(ASSISTANT_WAKE_PCM_PROBE)
static bool g_probe_active;
#endif

esp_err_t wake_word_init() {
    g_models = esp_srmodel_init("model");
    if (!g_models) { ESP_LOGE(TAG, "ESP-SR model 分区初始化失败"); return ESP_ERR_NOT_FOUND; }
    // 模型分区可能同时包含 mn7_cn；历史实机证据表明当前口令应使用 mn5q8_cn。
    // esp_srmodel_filter 按分区列表顺序返回首个匹配项，不能依赖打包顺序选择模型。
    g_model_name = esp_srmodel_filter(g_models, ESP_MN_PREFIX, kPreferredModelName);
#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
    if (!g_model_name) { ESP_LOGE(TAG, "P4/S31 requires the Chinese mn7 model partition"); return ESP_ERR_NOT_FOUND; }
#endif
    bool preferred_model = g_model_name != nullptr;
    if (!g_model_name) g_model_name = esp_srmodel_filter(g_models, ESP_MN_PREFIX, kFallbackModelName);
    if (!g_model_name) g_model_name = esp_srmodel_filter(g_models, ESP_MN_PREFIX, "cn");
    if (!g_model_name) { ESP_LOGE(TAG, "model 分区没有中文MultiNet，需同步烧录语音模型"); return ESP_ERR_NOT_FOUND; }
    if (!preferred_model) ESP_LOGW(TAG, "首选中文MultiNet=%s 不可用，回退到模型列表中的 %s", kPreferredModelName, g_model_name);
    // mn6_cn 分数分布与 mn5q8 不同，回退时保留既有验证阈值。
    if (!preferred_model) g_threshold = 0.20f;
    g_iface = esp_mn_handle_from_name(g_model_name);
    if (!g_iface) { ESP_LOGE(TAG, "中文短语识别模型不可用"); return ESP_ERR_NOT_FOUND; }
    g_model = g_iface->create(g_model_name, kModelDurationMs); if (!g_model) return ESP_ERR_NO_MEM;
    // 按小智顺序先设置检测阈值，再生成命令图，避免命令图保留默认阈值。
    g_iface->set_det_threshold(g_model, g_threshold.load());
    if (esp_mn_commands_alloc(g_iface, g_model) != ESP_OK || esp_mn_commands_clear() != ESP_OK || esp_mn_commands_add(1, kWakePhrase) != ESP_OK || esp_mn_commands_update() != nullptr) {
        ESP_LOGE(TAG, "伊迪斯离线口令注册失败");
        esp_mn_commands_free(); g_iface->destroy(g_model); g_model = nullptr; return ESP_FAIL;
    }
    g_iface->print_active_speech_commands(g_model);
    g_chunk = g_iface->get_samp_chunksize(g_model);
    if (g_chunk <= 0 || g_chunk > static_cast<int>(kPendingCapacity)) {
        ESP_LOGE(TAG, "MultiNet chunk 超过本地有界缓冲");
        g_iface->destroy(g_model); g_model = nullptr;
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "local_phrase_ready phrase=ni_hao_yi_di_si model=%s preferred=%d chunk=%d rate=%d threshold=%.2f duration_ms=%d", g_model_name, preferred_model, g_chunk, g_iface->get_samp_rate(g_model), static_cast<double>(g_threshold.load()), kModelDurationMs);
    wake_word_reset();
    return g_iface->get_samp_rate(g_model) == 16000 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

void wake_word_reset() {
    if (g_iface && g_model) g_iface->clean(g_model);
    g_pending_count = 0;
    g_detect_total_us = 0;
    g_detect_calls = 0;
    g_detect_max_us = 0;
    g_input_square = 0;
    g_input_samples = 0;
    g_input_peak = 0;
    g_input_frames = 0;
    g_pending_resets = 0;
}

void wake_word_set_threshold(float threshold) {
    if (threshold < 0.05f || threshold > 0.95f) return;
    g_threshold = threshold;
    if (g_iface && g_model) g_iface->set_det_threshold(g_model, threshold);
    ESP_LOGI(TAG, "wake_threshold_set value=%.2f", static_cast<double>(threshold));
}

bool wake_word_feed(const int16_t *samples, size_t sample_count) {
    if (!g_iface || !g_model || !samples || g_chunk <= 0) return false;
    if (sample_count > kPendingCapacity) return false;
    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t value = samples[index];
        g_input_square += static_cast<uint64_t>(value * value);
        const uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);
        if (magnitude > g_input_peak) g_input_peak = magnitude;
    }
    g_input_samples += static_cast<uint32_t>(sample_count);
    ++g_input_frames;
    if (g_input_frames >= 50) {
        const uint32_t rms = g_input_samples ? static_cast<uint32_t>(sqrt(static_cast<double>(g_input_square) / g_input_samples)) : 0;
        ESP_LOGI(TAG, "wake_input_stats calls=%lu rms=%lu peak=%lu pending_resets=%lu", static_cast<unsigned long>(g_input_frames), static_cast<unsigned long>(rms), static_cast<unsigned long>(g_input_peak), static_cast<unsigned long>(g_pending_resets));
        g_input_square = 0;
        g_input_samples = 0;
        g_input_peak = 0;
        g_input_frames = 0;
        g_pending_resets = 0;
    }
    if (sample_count > kPendingCapacity - g_pending_count) { ++g_pending_resets; g_pending_count = 0; }
    // 与小智 v2.5.2 CustomWakeWord::FeedSamples 一致，按原始 AFE 输出喂入 MultiNet：
    // https://github.com/78/xiaozhi-esp32/blob/v2.5.2/main/audio/wake_words/custom_wake_word.cc
    memcpy(g_pending + g_pending_count, samples, sample_count * sizeof(int16_t));
    g_pending_count += sample_count;
    while (g_pending_count >= static_cast<size_t>(g_chunk)) {
        const int64_t begin = esp_timer_get_time();
        const esp_mn_state_t state = g_iface->detect(g_model, g_pending);
        const uint32_t elapsed = esp_timer_get_time() - begin;
        g_detect_total_us += elapsed; ++g_detect_calls; if (elapsed > g_detect_max_us) g_detect_max_us = elapsed;
        if (g_detect_calls == 128) {
            ESP_LOGI(TAG, "wake_inference avg_us=%lu max_us=%lu audio_chunk_us=32000", static_cast<unsigned long>(g_detect_total_us / g_detect_calls), static_cast<unsigned long>(g_detect_max_us));
            g_detect_calls = 0; g_detect_total_us = 0; g_detect_max_us = 0;
        }
        if (state == ESP_MN_STATE_DETECTED) {
            const esp_mn_results_t *result = g_iface->get_results(g_model);
            int matched_index = -1;
            float matched_confidence = 0.0f;
            if (result) {
                for (int index = 0; index < result->num; ++index) {
                    if (result->command_id[index] == 1) {
                        matched_index = index;
                        matched_confidence = result->prob[index];
                        break;
                    }
                }
            }
            const int matched_command_id = matched_index >= 0 ? result->command_id[matched_index] : -1;
            ESP_LOGI(TAG, "wake_candidate count=%d id=%d confidence=%.4f", result ? result->num : 0, matched_command_id, static_cast<double>(matched_confidence));
            g_iface->clean(g_model);
            if (matched_index >= 0) {
                g_pending_count = 0;
                ESP_LOGI(TAG, "local_phrase_detected confidence=%.3f", static_cast<double>(matched_confidence));
                return true;
            }
        }
        if (state == ESP_MN_STATE_TIMEOUT) {
#if defined(ASSISTANT_WAKE_PCM_PROBE)
            if (g_probe_active) { const auto *result = g_iface->get_results(g_model); ESP_LOGI(TAG, "wake_probe_hypothesis=%s", result ? result->string : ""); }
#endif
            ESP_LOGD(TAG, "wake_timeout reset=1");
            g_iface->clean(g_model);
        }
        const size_t remain = g_pending_count - static_cast<size_t>(g_chunk);
        memmove(g_pending, g_pending + g_chunk, remain * sizeof(int16_t));
        g_pending_count = remain;
    }
    return false;
}

#if defined(ASSISTANT_WAKE_PCM_PROBE)
static bool wake_word_feed_probe(const int16_t *samples, size_t sample_count) {
    wake_word_reset();
    g_probe_active = true;
    bool detected = false;
    for (size_t offset = 0; offset < sample_count && !detected; offset += kProbeFrameSamples) {
        const size_t frame = sample_count - offset < kProbeFrameSamples ? sample_count - offset : kProbeFrameSamples;
        detected = wake_word_feed(samples + offset, frame);
        vTaskDelay(1);
    }
    int16_t silence[kProbeFrameSamples] = {};
    for (int frame = 0; frame < 50 && !detected; ++frame) { detected = wake_word_feed(silence, kProbeFrameSamples); vTaskDelay(1); }
    const auto *result = g_iface->get_results(g_model);
    ESP_LOGI(TAG, "wake_probe_last=%s", result ? result->string : "");
    g_probe_active = false;
    wake_word_reset();
    return detected;
}

esp_err_t wake_word_run_pcm_probe(bool *negative_detected, bool *positive_detected) {
    if (!negative_detected || !positive_detected) return ESP_ERR_INVALID_ARG;
    if (!g_iface || !g_model) return ESP_ERR_INVALID_STATE;
    const int64_t begin = esp_timer_get_time();
    *negative_detected = wake_word_feed_probe(probe_negative, sizeof(probe_negative) / sizeof(probe_negative[0]));
    *positive_detected = wake_word_feed_probe(probe_positive, sizeof(probe_positive) / sizeof(probe_positive[0]));
    ESP_LOGI(TAG, "wake_pcm_probe negative=%d positive=%d elapsed_ms=%lld", *negative_detected, *positive_detected, (esp_timer_get_time() - begin) / 1000);
    return ESP_OK;
}
#endif
