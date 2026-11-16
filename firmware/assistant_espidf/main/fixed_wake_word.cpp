#include "wake_word.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "esp_log.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

#if !(CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6)
#error "This fixed WakeNet candidate is intended only for the reviewed C3/C5/C6 ports"
#endif

namespace {
constexpr const char *kTag = "fixed_wake";
// [人工决策-2026-11-12 10:07:55] 牛牛接受C3/C5/C6使用库支持的固定词，逐板标明差异。
// The reviewed Chinese chip default is this exact model, not a custom phrase.
constexpr const char *kModelName = "wn9s_nihaoxiaozhi";
constexpr const char *kWakePhrase = "你好小智";
constexpr size_t kPendingCapacity = 1024;
constexpr size_t kMaxFeedSamples = 2048;
constexpr float kStandbyRequest = 0.20f;
// Technical candidate margin only, not acoustically calibrated. MultiNet's
// numeric thresholds cannot be reused as WakeNet confidence thresholds.
constexpr float kPlaybackMargin = 0.05f;
std::atomic<bool> g_playback_threshold{false};
std::atomic<bool> g_threshold_dirty{true};
const esp_wn_iface_t *g_iface;
model_iface_data_t *g_model;
srmodel_list_t *g_models;
char *g_model_name;
int16_t g_pending[kPendingCapacity];
size_t g_pending_count;
int g_chunk;
float g_default_threshold;
bool g_attempted, g_threshold_valid;
esp_err_t g_init_result = ESP_ERR_INVALID_STATE;

void release_model() {
    if (g_model) g_iface->destroy(g_model);
    g_model = nullptr;
    if (g_models) esp_srmodel_deinit_with_refcount(g_models);
    g_models = nullptr; g_model_name = nullptr; g_iface = nullptr;
    g_pending_count = 0; g_chunk = 0; g_threshold_valid = false;
}

bool apply_threshold() {
    const bool playback = g_playback_threshold.load();
    const float threshold = std::min(0.9999f, g_default_threshold + (playback ? kPlaybackMargin : 0.0f));
    g_threshold_valid = g_iface->set_det_threshold(g_model, threshold, 1) == 1;
    if (!g_threshold_valid) ESP_LOGE(kTag, "threshold rejected mode=%s value=%.4f; wake detection stopped", playback ? "playback" : "standby", static_cast<double>(threshold));
    else ESP_LOGI(kTag, "threshold mode=%s value=%.4f acoustic_acceptance=not_run", playback ? "playback" : "standby", static_cast<double>(threshold));
    return g_threshold_valid;
}
}

// Init/reset/feed belong to the recognition consumer. The threshold setter
// only records atomics; the consumer owns every model operation.
esp_err_t wake_word_init() {
    if (g_attempted) return g_init_result;
    g_attempted = true;
    g_models = esp_srmodel_init("model");
    if (!g_models || g_models->num <= 0) {
        ESP_LOGE(kTag, "fixed wake model partition missing or empty");
        release_model();
        return g_init_result = ESP_ERR_NOT_FOUND;
    }
    // Filter is a substring search in ESP-SR; verify the complete name so an
    // accidentally loaded larger/different model cannot be selected silently.
    for (int index = 0; index < g_models->num; ++index) {
        if (std::strcmp(g_models->model_name[index], kModelName) == 0) {
            g_model_name = g_models->model_name[index];
            break;
        }
    }
    if (!g_model_name) { release_model(); return g_init_result = ESP_ERR_NOT_FOUND; }
    char *words = esp_srmodel_get_wake_words(g_models, g_model_name);
    const bool correct_word = words && std::strcmp(words, kWakePhrase) == 0;
    std::free(words); // ESP-SR allocates this text from _MODEL_INFO_.
    if (!correct_word) { release_model(); return g_init_result = ESP_ERR_INVALID_RESPONSE; }
    g_iface = esp_wn_handle_from_name(g_model_name);
    if (!g_iface) { release_model(); return g_init_result = ESP_ERR_NOT_SUPPORTED; }
    g_model = g_iface->create(g_model_name, DET_MODE_90);
    if (!g_model) { release_model(); return g_init_result = ESP_ERR_NO_MEM; }
    g_chunk = g_iface->get_samp_chunksize(g_model);
    if (g_chunk <= 0 || g_chunk > static_cast<int>(kPendingCapacity) ||
        g_iface->get_samp_rate(g_model) != 16000 || g_iface->get_channel_num(g_model) != 1 ||
        g_iface->get_word_num(g_model) != 1) {
        ESP_LOGE(kTag, "fixed wake requires bounded16kHz mono single-word model");
        release_model();
        return g_init_result = ESP_ERR_NOT_SUPPORTED;
    }
    g_default_threshold = g_iface->get_det_threshold(g_model, 1);
    g_threshold_dirty.exchange(false);
    if (!std::isfinite(g_default_threshold) || g_default_threshold < 0.4f || g_default_threshold > 0.9999f || !apply_threshold()) {
        release_model();
        return g_init_result = ESP_ERR_INVALID_RESPONSE;
    }
    wake_word_reset();
    ESP_LOGI(kTag, "fixed_word_ready phrase=%s model=%s chunk=%d rate=16000; target_and_acoustic_acceptance=not_run", kWakePhrase, g_model_name, g_chunk);
    return g_init_result = ESP_OK;
}

void wake_word_reset() {
    if (g_model) g_iface->clean(g_model);
    g_pending_count = 0;
}

void wake_word_set_threshold(float threshold) {
    if (!std::isfinite(threshold) || threshold < 0.05f || threshold > 0.95f) return;
    // Current caller requests0.20 standby /0.30 playback. Preserve those
    // intents using this model's own baseline rather than using0.20/0.30.
    const bool playback = threshold > kStandbyRequest;
    if (g_playback_threshold.exchange(playback) != playback) g_threshold_dirty = true;
}

bool wake_word_feed(const int16_t *samples, size_t sample_count) {
    if (!g_model || !samples || sample_count == 0) return false;
    if (sample_count > kMaxFeedSamples) {
        wake_word_reset();
        ESP_LOGE(kTag, "feed exceeds bounded request; samples=%u", static_cast<unsigned>(sample_count));
        return false;
    }
    if (g_threshold_dirty.exchange(false) && !apply_threshold()) { wake_word_reset(); return false; }
    if (!g_threshold_valid) return false;
    size_t offset = 0;
    while (offset < sample_count) {
        const size_t copied = std::min(sample_count - offset, static_cast<size_t>(g_chunk) - g_pending_count);
        std::memcpy(g_pending + g_pending_count, samples + offset, copied * sizeof(int16_t));
        g_pending_count += copied;
        offset += copied;
        if (g_pending_count != static_cast<size_t>(g_chunk)) continue;
        const int detected = static_cast<int>(g_iface->detect(g_model, g_pending));
        g_pending_count = 0;
        if (detected == 1) {
            wake_word_reset();
            ESP_LOGI(kTag, "fixed_word_detected phrase=%s", kWakePhrase);
            return true;
        }
        if (detected != WAKENET_NO_DETECT && detected != WAKENET_CHANNEL_VERIFIED) {
            ESP_LOGE(kTag, "unexpected fixed wake result=%d; frame discarded", detected);
            wake_word_reset();
            return false;
        }
    }
    return false;
}

#if defined(ASSISTANT_WAKE_PCM_PROBE)
esp_err_t wake_word_run_pcm_probe(bool *negative_detected, bool *positive_detected) {
    if (!negative_detected || !positive_detected) return ESP_ERR_INVALID_ARG;
    *negative_detected = false; *positive_detected = false;
    // Existing positive fixture says nihaoxiaoyanzi, not this model's phrase.
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
