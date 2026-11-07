#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t wake_word_init();
// Call from the recognition consumer task when the recognition session changes.
void wake_word_reset();
bool wake_word_feed(const int16_t *samples, size_t sample_count);
// 播放期间以更低阈值识别打断唤醒词；回待机恢复常规阈值。
void wake_word_set_threshold(float threshold);

#if defined(ASSISTANT_WAKE_PCM_PROBE)
esp_err_t wake_word_run_pcm_probe(bool *negative_detected, bool *positive_detected);
#endif
