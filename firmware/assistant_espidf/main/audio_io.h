#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

struct AudioCaptureMetrics {
    bool voice_active;
    uint32_t mic_rms;
    uint32_t reference_rms;
    uint32_t clean_rms;
    float echo_correlation;
    uint32_t reference_delay_samples;
    bool playback_active;
};

struct AudioCaptureFrame {
    // 标签对应本次读出的全部 PCM；VAD 变化时本次读取可提前结束。
    bool vad_valid;
    bool voice_active;
    // 首帧、清空及溢出后为 false；调用方必须重新积累静音边界。
    bool continuous;
    uint32_t generation;
    uint64_t sample_index;
};

struct AudioCapturePending {
    uint32_t generation;
    uint64_t sample_index;
    size_t bytes;
};

esp_err_t audio_io_init();
esp_err_t audio_capture_start();
void audio_capture_stop();
void audio_capture_discard_pending();
bool audio_capture_pending(AudioCapturePending *pending);
bool audio_capture_failed();
size_t audio_capture_read(uint8_t *buffer, size_t capacity, AudioCaptureFrame *frame = nullptr);
esp_err_t audio_play_pcm24(uint8_t *data, size_t length);
esp_err_t audio_prepare_pcm24();
void audio_playback_abort();
bool audio_echo_cancellation_ready();
bool audio_capture_voice_active();
bool audio_capture_metrics(AudioCaptureMetrics *metrics);
esp_err_t audio_playback_pause(bool paused);
esp_err_t audio_set_output_preferences(uint8_t conversation_volume, uint8_t reminder_volume, bool conversation_muted, bool reminders_muted);
esp_err_t audio_set_notification_output(bool notification);
esp_err_t audio_adjust_conversation_volume(int delta, uint8_t *new_volume);
esp_err_t audio_diagnostic_start(uint32_t frame_count);
size_t audio_diagnostic_read(uint8_t *dst, size_t capacity);
bool audio_diagnostic_ready();
