#include "wake_word.h"

#include "esp_log.h"

esp_err_t wake_word_init() {
    ESP_LOGI("wake", "local wake disabled: manual audio profile");
    return ESP_ERR_NOT_SUPPORTED;
}
void wake_word_reset() {}
void wake_word_set_threshold(float) {}
bool wake_word_feed(const int16_t *, size_t) { return false; }

#if defined(ASSISTANT_WAKE_PCM_PROBE)
esp_err_t wake_word_run_pcm_probe(bool *negative_detected, bool *positive_detected) {
    if (negative_detected) *negative_detected = false;
    if (positive_detected) *positive_detected = false;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
