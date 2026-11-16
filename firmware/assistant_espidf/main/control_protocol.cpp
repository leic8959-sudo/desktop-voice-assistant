#include "control_protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <atomic>
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "device_websocket_client.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "audio_io.h"
#include "display_ui.h"
#include "ota_update.h"
#include "storage.h"
#include "wake_word.h"
#include "notification_cues.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "board_profile.h"
#if defined(ASSISTANT_WAKE_PCM_PROBE)
#include "lvgl.h"
#endif

static const char *TAG = "protocol";
static esp_websocket_client_handle_t g_ws;
static DeviceWebSocketClient g_ws_client;
static std::atomic<bool> g_listening{false};
static std::atomic<bool> g_speaking{false};
static std::atomic<bool> g_natural_barge_in{false};
static char g_voice_session_id[65];
static std::atomic<UiState> g_requested_ui{UiState::Idle};
static std::atomic<bool> g_auto_session{false};
static std::atomic<bool> g_wake_reset_pending{false};
static std::atomic<int64_t> g_playback_started_us{0};
static std::atomic<int64_t> g_first_pcm_received_us{0};
static std::atomic<int64_t> g_tts_received_us{0};
// 0.3.5 新增：播放结束（g_speaking 由 true 变 false）的时刻（esp_timer 微秒）。
// codec/I2S 在 g_speaking=false 之后仍有约 100-300ms 尾音，采集循环要据此继续清零上传。
static std::atomic<int64_t> g_playback_end_us{0};
static std::atomic<bool> g_wake_upload_pending{false};
static std::atomic<bool> g_wake_upload_blocked{false};
static std::atomic<bool> g_wake_upload_release_pending{false};
static std::atomic<bool> g_abort_requested{false};
static char g_navigation_id[40];
static constexpr size_t WAKE_CACHE_BYTES = 16000 * 2 * 2;
static constexpr size_t WAKE_PREFIX_BYTES = 16000 * 2 * 4;
static uint8_t *g_wake_cache;
static size_t g_wake_cache_write;
static size_t g_wake_cache_count;
static uint8_t *g_wake_prefix_cache;
static size_t g_wake_prefix_write;
static size_t g_wake_prefix_count;
static bool g_wake_prefix_frozen;
static std::atomic<bool> g_wake_clip_supported{false};
static std::atomic<bool> g_wake_input_ready_supported{false};
static bool g_wake_clip_active;
static bool g_wake_cache_overflow;
static uint32_t g_wake_clip_samples;
static uint32_t g_wake_clip_quiet_samples;
static uint32_t g_wake_clip_sent_bytes;
static constexpr uint32_t WAKE_CLIP_QUIET_SAMPLES = 16000 * 300 / 1000;
static constexpr uint32_t WAKE_CLIP_LIMIT_SAMPLES = 16000 * 8;
static uint32_t g_next_wake_id;
static char g_wake_id[32];
static std::atomic<int64_t> g_wake_detected_us{0};
static std::atomic<int64_t> g_wake_cloud_ready_us{0};
static std::atomic<int64_t> g_wake_input_ready_us{0};
static std::atomic<int64_t> g_wake_first_upload_us{0};
static std::atomic<int64_t> g_wake_first_upload_done_us{0};
static std::atomic<int64_t> g_wake_tx_total_us{0};
static std::atomic<int64_t> g_wake_tx_max_us{0};
// 前缀4秒加最多4秒积压；满队列结束本候选，不覆盖或跳过语音。
struct WakeTxChunk { uint32_t generation; uint32_t audio_bytes; uint32_t ended_ms; uint16_t length; bool limited; bool manual_start; bool manual_end; uint8_t data[640]; };
// Internal mode reserves32 audio frames plus one control slot. At20ms per
// frame this bounds pre-ready/backpressure buffering to640ms; existing queue
// overflow handling cancels the request instead of silently dropping PCM.
static constexpr size_t WAKE_TX_AUDIO_CAPACITY = board_profile::manual_audio_only ? 32 : 400;
static constexpr size_t WAKE_TX_BATCH_BYTES = 3840;
static QueueHandle_t g_wake_tx_queue;
static StaticQueue_t g_wake_tx_queue_control;
static std::atomic<uint32_t> g_wake_tx_generation{0};
static std::atomic<bool> g_wake_tx_enabled{false};
static std::atomic<bool> g_wake_tx_ready{false};
static std::atomic<bool> g_wake_tx_failed{false};
static std::atomic<bool> g_wake_tx_inflight{false};
static bool g_wake_tx_release_waiting;
static bool g_wake_tx_copying_prefix;
static uint32_t g_wake_tx_sent_bytes;
static uint32_t g_wake_tx_queued_bytes;
static std::atomic<bool> g_manual_input_ready_supported{false};
static std::atomic<bool> g_manual_held{false};
static std::atomic<uint32_t> g_manual_request{0};
static std::atomic<uint32_t> g_manual_media_ready{0};
static std::atomic<uint32_t> g_manual_media_request{0};
static std::atomic<bool> g_manual_cancel_requested{false};
static std::atomic<bool> g_manual_active{false};
static std::atomic<bool> g_manual_start_sent{false};
static std::atomic<bool> g_manual_end_sent{false};
static std::atomic<uint32_t> g_manual_owner_request{0};
static std::atomic<uint32_t> g_manual_ready_ack_token{0};
static bool g_manual_media_waiting;
static bool g_manual_ended;
static bool g_manual_releasing;
static AudioCapturePending g_manual_pending;
static bool g_manual_capture_seen;
static uint32_t g_manual_capture_generation;
static uint64_t g_manual_capture_expected_sample;
static int64_t g_manual_started_us;
static int64_t g_manual_ended_us;
static char g_manual_id[32];
static uint32_t g_manual_captured_bytes;
static uint32_t g_wake_boundary_quiet_samples;
static bool g_wake_boundary_reset;
static bool g_wake_boundary_seen;
static uint32_t g_wake_boundary_generation;
static uint64_t g_wake_boundary_expected_sample;
// 只由采音任务切换，保存原模式，空闲恢复；不创建额外任务或重试循环。
static bool g_wifi_active_requested;
static bool g_wifi_original_ps_known;
static wifi_ps_type_t g_wifi_original_ps;
static char g_headers[256];
static char g_device_token[200];
struct AudioChunk { size_t length; uint32_t generation; uint8_t data[2048]; bool chime; bool media; };
static QueueHandle_t g_audio_queue;
static StaticQueue_t g_audio_queue_control;
static AudioChunk g_incoming_chunk;
static char g_text_fragments[6144];
static size_t g_text_fragment_length;
static bool g_text_fragment_active;
static bool g_reject_text_fragment;
static std::atomic<bool> g_cloud_ready{false};
static std::atomic<uint32_t> g_generation{0};
static std::atomic<bool> g_response_finished{false};
static std::atomic<uint32_t> g_queue_overflows{0};
enum class BargeState : uint8_t { None, Candidate, Confirmed };
static std::atomic<BargeState> g_barge_state{BargeState::None};
static std::atomic<uint32_t> g_next_candidate_id{0};
static std::atomic<uint32_t> g_barge_candidate_id{0};
static std::atomic<uint32_t> g_barge_played_samples{0};
static std::atomic<bool> g_barge_confirmed_ack{false};
static std::atomic<bool> g_barge_resume_ack{false};
static int64_t g_barge_started_us;
static uint32_t g_barge_voice_us;
static uint32_t g_barge_voice_gap_us;
static int64_t g_barge_cooldown_until_us;
static uint32_t g_clean_rms_baseline = 600;
static uint32_t g_echo_floor_rms;
static uint32_t g_candidate_clean_rms;
static uint32_t g_candidate_peak_rms;
static std::atomic<bool> g_drop_old_audio{false};
static std::atomic<bool> g_diagnostics{false};
static std::atomic<bool> g_test_disable_barge{false};
static std::atomic<bool> g_audio_diagnostic_requested{false};
static std::atomic<bool> g_audio_diagnostic_export{false};
static uint32_t g_audio_diagnostic_chunks;
static uint32_t g_last_diagnostic;
static char g_playback_id[64];
static uint8_t *g_barge_cache;
static size_t g_barge_cache_write;
static size_t g_barge_cache_count;
static constexpr size_t BARGE_PREROLL_BYTES = 16000 * 2 * 3 / 5;
// 0.3.5 播放结束尾音静音窗口：0.3.4 网关日志（会话 09dd5247…，本地时间
// 10:48:54-10:50:26 连续 9 轮回答、全程无任何真人打断）证明——tts stop 时
// g_speaking 先置 false，但编解码器/I2S 还有约 100-300ms 尾音；上行立即恢复后
// 这段尾音被云端 qwen server_vad（threshold 0.75 / 静音 600ms）判成人声，
// 云端自动生成回答，回答播完再触发，形成"自问自答"无限循环。
// 0.3.4 的"播放期上传置零"只覆盖到 g_speaking=false 为止，这里在结束时刻后再静音
// 400ms 上传：memset 后照常发送、绝不 skip，否则云端会以为上行断流。
// 参照 docs/自然插话尝试与播放期打断失稳20261101.md 的 0.3.4 结论。
static constexpr int64_t kUplinkEchoTailUs = 400000;
static constexpr uint32_t BARGE_TAIL_US = 300000;
static constexpr uint32_t BARGE_VOICE_CONFIRM_US = 120000;
static constexpr uint32_t BARGE_RESUME_US = 700000;
// 播放期上传的本地残留门槛：低于此值视为回声残留置零上传。
// AEC VERYAGGR 后残留实测 400-800，真人 12dB 下 3000+，1200 居中。
// 本地强打断：持续400ms超过3000才本地取消（残留峰值帧稀疏，连续概率低）；
// 中低电平的自然语速交给云端 VAD 判定。
static constexpr uint32_t kLoudInterruptRms = 3000;
static constexpr uint32_t kLoudInterruptHoldUs = 400000;
// 0.3.6：语音打断（barge-in）编译期总开关。牛牛实测反馈"打断不行"，要求回答必须
// 完整播完、播完后再听用户，因此本版先关闭：说话播放期间只上传静音帧，不跑本地
// 强打断（kLoudInterruptRms）、不喂唤醒词、不触发 start_barge_candidate/confirm_barge/
// abort。恢复语音打断只需把这一个常量改回 true（下方 Candidate/Confirmed 分支与
// 唤醒词路径都完整保留）；物理按键 control_protocol_listen_start() 不受此开关影响。
static constexpr bool kEnableBargeIn = false;
static uint32_t g_loud_hold_us;
static constexpr int AUDIO_QUEUE_CAPACITY = board_profile::manual_audio_only ? 8 : 32;
enum class MediaCommandKind : uint8_t { Start, Pause, Stop, End, Wake, Manual, Error };
struct MediaCommand { MediaCommandKind kind; char playback_id[64]; char title[193]; uint32_t position_samples; char error[32]; uint32_t manual_request; };
struct MediaReceipt { char state[12]; char playback_id[64]; char reason[16]; char error[32]; uint32_t played_samples; uint32_t underruns; uint32_t queue_used; bool listen; bool manual; uint32_t manual_request; };
static uint32_t g_media_manual_request;
static QueueHandle_t g_media_commands;
static StaticQueue_t g_media_commands_control;
EXT_RAM_BSS_ATTR static uint8_t g_media_commands_storage[4 * sizeof(MediaCommand)];
static QueueHandle_t g_media_receipts;
static StaticQueue_t g_media_receipts_control;
EXT_RAM_BSS_ATTR static uint8_t g_media_receipts_storage[8 * sizeof(MediaReceipt)];
static std::atomic<bool> g_media_active{false};
static std::atomic<bool> g_media_wake_pending{false};
static std::atomic<bool> g_media_error_pending{false};
static std::atomic<uint32_t> g_media_played_samples{0};
static std::atomic<uint32_t> g_media_underruns{0};
static std::atomic<int64_t> g_media_echo_until_us{0};
static char g_media_id[64];
static char g_media_title[193];
static bool g_media_end_received;
static int64_t g_media_progress_us;
static bool g_wake_word_ready;
// 待机与播放期使用不同唤醒阈值：播放期增益已降到12dB且有回声残留，
// 打断是显式意图，宁可降低门槛也要保证说完整唤醒词能打断回答。
static constexpr float kWakeThresholdStandby = 0.20f;
// 0.12 实测被播放内容残留穿透（假阳性 0.1516）；播放期真词实测 0.4289（喇叭口径，
// 真人更低），0.25 折衷；自然插话候选作为第二通道兜底。
static constexpr float kWakeThresholdInterrupt = 0.30f;
static bool g_interrupt_threshold_active;
// 提示音由独立短任务播放（走 audio_play_pcm24，与 TTS 同链路），不进音频队列，
// 因此与 playback_task 的预缓冲逻辑完全无关；g_chime_playing 防止重入。
static std::atomic<bool> g_chime_playing{false};
static std::atomic<bool> g_dnd_active{false};
static std::atomic<int64_t> g_chime_echo_until_us{0};
// 断线自愈：对端进程死亡可能留下 TCP 半开连接，WS 事件和传输层超时都暴露不了。
// 设备周期发送 device_ping，超过 kWsDownlinkRestartUs 没有任何下行（含 device_pong）
// 就整体重启；代价是数分钟的重连窗口，收益是断线必然恢复。
static std::atomic<int64_t> g_last_downlink_us{0};
static constexpr int64_t kWsDownlinkRestartUs = 240000000LL;
static constexpr int64_t kWsPingIntervalUs = 30000000LL;

static void begin_listening(bool manual);
static void reset_barge_state(bool restore_playback);
static bool finish_wake_clip();
static bool queue_wake_audio(const uint8_t *data, size_t length);
static void abort_wake_audio();
static void service_manual_listening();
static void note_wake_boundary_audio(size_t bytes, const AudioCaptureFrame &frame);

// 0.3.5：记录"播放真正结束"的时刻。所有停播路径（播放自然结束、tts stop、
// interrupt、abort、断线、队列溢出）都走这里；只有 g_speaking 由 true 变 false
// 才刷新时间戳，避免空闲时反复延后窗口。先写时间戳再落 g_speaking，采集线程
// 不会看到"已静音但窗口未生效"的空隙。
static void note_playback_stopped() {
    if (!g_speaking.load()) return;
    g_playback_end_us = esp_timer_get_time();
    g_speaking = false;
}

static bool send_json(cJSON *root) {
    if (!g_ws || !esp_websocket_client_is_connected(g_ws)) { cJSON_Delete(root); return false; }
    char *json = cJSON_PrintUnformatted(root);
    bool sent = false;
    if (json) { const size_t length = strlen(json); sent = esp_websocket_client_send_text(g_ws, json, length, pdMS_TO_TICKS(1000)) == static_cast<int>(length); free(json); }
    cJSON_Delete(root);
    return sent;
}

static void send_hello() {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "board", board_profile::name);
#if defined(ASSISTANT_BOARD_FIXED_WAKE)
    cJSON_AddStringToObject(root, "wake_phrase", "你好小智");
#else
    cJSON_AddStringToObject(root, "wake_phrase", "你好，伊迪斯");
#endif
    cJSON *display = cJSON_AddObjectToObject(root, "display");
    cJSON_AddNumberToObject(display, "width", board_profile::width);
    cJSON_AddNumberToObject(display, "height", board_profile::height);
    cJSON_AddBoolToObject(display, "round", board_profile::round);
    cJSON_AddStringToObject(root, "type", "hello"); cJSON_AddNumberToObject(root, "version", 1); cJSON_AddStringToObject(root, "transport", "websocket");
    #ifdef ASSISTANT_FW_VERSION
    cJSON_AddStringToObject(root, "firmware_version", ASSISTANT_FW_VERSION);
#else
    cJSON_AddStringToObject(root, "firmware_version", esp_app_get_description()->version);
#endif
    cJSON *audio = cJSON_AddObjectToObject(root, "audio_params"); cJSON_AddStringToObject(audio, "format", "pcm_s16le"); cJSON_AddNumberToObject(audio, "sample_rate", 16000); cJSON_AddNumberToObject(audio, "channels", 1); cJSON_AddNumberToObject(audio, "frame_duration", 20);
    cJSON *features = cJSON_AddObjectToObject(root, "features"); cJSON_AddBoolToObject(features, "custom_ui", true); cJSON_AddBoolToObject(features, "screen_cards", true);
#if defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_INTERNAL_RAM)
    cJSON_AddBoolToObject(features, "two_stage_barge_in", false);
    cJSON_AddStringToObject(features, "barge_in_trigger", "manual");
#else
    cJSON_AddBoolToObject(features, "two_stage_barge_in", true);
    cJSON_AddStringToObject(features, "barge_in_trigger", "wake_word");
#endif
    cJSON_AddBoolToObject(features, "natural_barge_in_v1", audio_echo_cancellation_ready());
    cJSON_AddBoolToObject(features, "media_stream", true);
    cJSON_AddBoolToObject(features, "media_wake_word", g_wake_word_ready && audio_echo_cancellation_ready());
    cJSON *power = cJSON_AddObjectToObject(root, "power"); cJSON_AddStringToObject(power, "status", "unknown"); cJSON_AddStringToObject(power, "source", "board_power_readback_unavailable");
#if defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_INTERNAL_RAM)
    cJSON_AddBoolToObject(features, "wake_clip_v1", false);
    cJSON_AddBoolToObject(features, "wake_input_ready_v1", false);
#else
    cJSON_AddBoolToObject(features, "wake_clip_v1", true);
    cJSON_AddBoolToObject(features, "wake_input_ready_v1", true);
#endif
    cJSON_AddBoolToObject(features, "manual_input_ready_v1", true);
    cJSON_AddBoolToObject(features, "software_aec", audio_echo_cancellation_ready()); cJSON_AddStringToObject(features, "aec_reference", board_profile::manual_audio_only ? "none" : "software_pcm24"); send_json(root);
}

static void send_control(const char *type, const char *state = nullptr, const char *mode = nullptr) {
    cJSON *root = cJSON_CreateObject(); cJSON_AddStringToObject(root, "type", type);
    if (state) { cJSON_AddStringToObject(root, "state", state); }
    if (mode) { cJSON_AddStringToObject(root, "mode", mode); }
    send_json(root);
}

static void confirm_wake_audio_release() {
    g_wake_upload_release_pending = true;
}

static bool accept_wake_listening() {
    if (g_wake_upload_blocked.load()) {
        ESP_LOGW(TAG, "wake_upload_late_listening_ignored");
        return false;
    }
    if (!g_cloud_ready.load() && g_wake_detected_us.load()) g_wake_cloud_ready_us = esp_timer_get_time();
    g_cloud_ready = true;
    if (g_auto_session) g_listening = true;
    return true;
}

static void send_wake_detect() {
    if (g_wake_upload_blocked.load()) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "listen");
    cJSON_AddStringToObject(root, "state", "detect");
    cJSON_AddStringToObject(root, "text", "你好，伊迪斯");
    cJSON_AddStringToObject(root, "wake_id", g_wake_id);
    send_json(root);
}

static void send_media_ack(const char *state, const char *reason = nullptr, const char *error = nullptr, const char *playback_id = nullptr) {
    MediaReceipt receipt = {};
    snprintf(receipt.state, sizeof(receipt.state), "%s", state);
    snprintf(receipt.playback_id, sizeof(receipt.playback_id), "%s", playback_id ? playback_id : g_media_id);
    if (reason) snprintf(receipt.reason, sizeof(receipt.reason), "%s", reason);
    if (error) snprintf(receipt.error, sizeof(receipt.error), "%s", error);
    receipt.played_samples = g_media_played_samples.load();
    receipt.underruns = g_media_underruns.load();
    receipt.queue_used = g_audio_queue ? uxQueueMessagesWaiting(g_audio_queue) : 0;
    receipt.listen = reason && (strcmp(reason, "wake_word") == 0 || strcmp(reason, "button") == 0);
    receipt.manual = reason && strcmp(reason, "button") == 0;
    receipt.manual_request = receipt.manual ? g_media_manual_request : 0;
    if (!g_media_receipts || xQueueSend(g_media_receipts, &receipt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "media_receipt_queue_full state=%s", state);
        if (receipt.listen) { g_media_wake_pending = false; g_wake_reset_pending = true; }
    }
}

static void service_media_receipts() {
    MediaReceipt receipt;
    while (xQueueReceive(g_media_receipts, &receipt, 0) == pdTRUE) {
        if (!g_ws || !esp_websocket_client_is_connected(g_ws)) { if (receipt.listen) { g_media_wake_pending = false; g_wake_reset_pending = true; } continue; }
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "media_ack");
        cJSON_AddStringToObject(root, "state", receipt.state);
        cJSON_AddStringToObject(root, "phase", receipt.state);
        cJSON_AddStringToObject(root, "playback_id", receipt.playback_id);
        cJSON_AddNumberToObject(root, "played_samples", receipt.played_samples);
        cJSON_AddNumberToObject(root, "position_samples", receipt.played_samples);
        cJSON_AddNumberToObject(root, "underruns", receipt.underruns);
        cJSON_AddNumberToObject(root, "queue_used", receipt.queue_used);
        cJSON_AddNumberToObject(root, "queue_capacity", g_audio_queue ? AUDIO_QUEUE_CAPACITY : 0);
        if (receipt.reason[0]) cJSON_AddStringToObject(root, "reason", receipt.reason);
        if (receipt.error[0]) cJSON_AddStringToObject(root, "error", receipt.error);
        const bool sent = send_json(root);
        if (receipt.listen && g_wake_upload_blocked.load()) {
            g_media_wake_pending = false;
            ESP_LOGW(TAG, "media_wake_start_blocked waiting_for_session_end");
            continue;
        }
        // 先回执暂停，再沿原入口发送 detect/start；采音任务仅缓存，不做网络等待。
        if (receipt.listen && sent) {
            if (receipt.manual) {
                if (receipt.manual_request == g_manual_media_request.load()) g_manual_media_ready = receipt.manual_request;
            }
            else { send_wake_detect(); g_wake_upload_pending = true; begin_listening(false); }
            g_media_wake_pending = false;
        } else if (receipt.listen) {
            g_media_wake_pending = false; g_wake_reset_pending = true;
            ESP_LOGW(TAG, "media_pause_receipt_failed wake_session_not_started");
        }
    }
}

static bool queue_media_command(const MediaCommand &command) {
    return g_media_commands && xQueueSend(g_media_commands, &command, 0) == pdTRUE;
}

static bool valid_media_text(const cJSON *value, size_t capacity, bool required) {
    if (!value) return !required;
    if (!cJSON_IsString(value) || strlen(value->valuestring) >= capacity || (required && !value->valuestring[0])) return false;
    for (const unsigned char *character = reinterpret_cast<const unsigned char *>(value->valuestring); *character; ++character) if (*character < 0x20) return false;
    return true;
}

static bool media_integer(const cJSON *value, uint32_t maximum) {
    return cJSON_IsNumber(value) && value->valuedouble >= 0 && value->valuedouble <= maximum && value->valuedouble == static_cast<uint32_t>(value->valuedouble);
}

static void receive_media_command(const cJSON *root, const char *type) {
    MediaCommand command = {};
    const cJSON *identity = cJSON_GetObjectItem(root, "playback_id");
    if (!valid_media_text(identity, sizeof(command.playback_id), true)) { send_media_ack("error", nullptr, "invalid_playback_id", ""); return; }
    snprintf(command.playback_id, sizeof(command.playback_id), "%s", identity->valuestring);
    if (strcmp(type, "media_start") == 0) {
        command.kind = MediaCommandKind::Start;
        const cJSON *title = cJSON_GetObjectItem(root, "title");
        const cJSON *format = cJSON_GetObjectItem(root, "format");
        const cJSON *rate = cJSON_GetObjectItem(root, "sample_rate");
        const cJSON *channels = cJSON_GetObjectItem(root, "channels");
        const cJSON *frame_ms = cJSON_GetObjectItem(root, "frame_duration_ms");
        const cJSON *position = cJSON_GetObjectItem(root, "position_samples");
        const cJSON *duration = cJSON_GetObjectItem(root, "duration_s");
        if (!valid_media_text(title, sizeof(command.title), false) || !cJSON_IsString(format) || strcmp(format->valuestring, "pcm_s16le") != 0 ||
            !media_integer(rate, 24000) || rate->valueint != 24000 || !media_integer(channels, 1) || channels->valueint != 1 ||
            !media_integer(frame_ms, 20) || frame_ms->valueint != 20 || !media_integer(position, 24000U * 86400U) ||
            (duration && !media_integer(duration, 86400))) { send_media_ack("error", nullptr, "invalid_audio_parameters", command.playback_id); return; }
        const char *allowed[] = {"type", "playback_id", "title", "format", "sample_rate", "channels", "frame_duration_ms", "position_samples", "duration_s"};
        for (const cJSON *field = root->child; field; field = field->next) {
            bool known = false;
            for (const char *key : allowed) if (field->string && strcmp(field->string, key) == 0) { known = true; break; }
            if (!known) { send_media_ack("error", nullptr, "unknown_start_field", command.playback_id); return; }
        }
        command.position_samples = static_cast<uint32_t>(position->valuedouble);
        snprintf(command.title, sizeof(command.title), "%s", title && title->valuestring[0] ? title->valuestring : "正在播放媒体");
    } else command.kind = strcmp(type, "media_pause") == 0 ? MediaCommandKind::Pause : strcmp(type, "media_stop") == 0 ? MediaCommandKind::Stop : MediaCommandKind::End;
    if (!queue_media_command(command)) send_media_ack("error", nullptr, "command_queue_full", command.playback_id);
}

static void send_barge_message(const char *phase) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "barge_in");
    cJSON_AddStringToObject(root, "phase", phase);
    cJSON_AddNumberToObject(root, "candidate_id", g_barge_candidate_id.load());
    cJSON_AddStringToObject(root, "playback_id", g_playback_id);
    cJSON_AddNumberToObject(root, "played_samples", g_barge_played_samples.load());
    send_json(root);
}

static bool same_barge_identity(const cJSON *root) {
    const cJSON *candidate = cJSON_GetObjectItem(root, "candidate_id");
    if (!cJSON_IsNumber(candidate) || candidate->valuedouble < 0 || candidate->valuedouble > UINT32_MAX || candidate->valuedouble != static_cast<uint32_t>(candidate->valuedouble) || static_cast<uint32_t>(candidate->valuedouble) != g_barge_candidate_id.load()) return false;
    const cJSON *playback = cJSON_GetObjectItem(root, "playback_id");
    return cJSON_IsString(playback) && strcmp(playback->valuestring, g_playback_id) == 0;
}

static void cache_barge_audio(const uint8_t *data, size_t length) {
    if (!g_barge_cache || !length) return;
    const size_t first = length < BARGE_PREROLL_BYTES - g_barge_cache_write ? length : BARGE_PREROLL_BYTES - g_barge_cache_write;
    memcpy(g_barge_cache + g_barge_cache_write, data, first);
    if (length > first) memcpy(g_barge_cache, data + first, length - first);
    g_barge_cache_write = (g_barge_cache_write + length) % BARGE_PREROLL_BYTES;
    g_barge_cache_count = g_barge_cache_count + length < BARGE_PREROLL_BYTES ? g_barge_cache_count + length : BARGE_PREROLL_BYTES;
}

static void upload_barge_cache() {
    if (!g_barge_cache || !g_barge_cache_count || !g_ws || !esp_websocket_client_is_connected(g_ws)) return;
    const size_t start = (g_barge_cache_write + BARGE_PREROLL_BYTES - g_barge_cache_count) % BARGE_PREROLL_BYTES;
    const size_t first = g_barge_cache_count < BARGE_PREROLL_BYTES - start ? g_barge_cache_count : BARGE_PREROLL_BYTES - start;
    esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(g_barge_cache + start), first, pdMS_TO_TICKS(1000));
    if (first < g_barge_cache_count) esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(g_barge_cache), g_barge_cache_count - first, pdMS_TO_TICKS(1000));
    g_barge_cache_count = 0;
    g_barge_cache_write = 0;
}

static void reset_barge_cache() {
    g_barge_cache_count = 0;
    g_barge_cache_write = 0;
}

static void reset_barge_state(bool restore_playback) {
    const BargeState previous = g_barge_state.exchange(BargeState::None);
    const int64_t started_us = g_barge_started_us;
    if (previous != BargeState::None) {
        (void)audio_playback_pause(false);
        ESP_LOGI(TAG, "barge_state reset previous=%d", static_cast<int>(previous));
    }
    g_barge_voice_us = 0;
    g_barge_voice_gap_us = 0;
    g_barge_resume_ack = false;
    g_barge_confirmed_ack = false;
    g_barge_started_us = 0;
    reset_barge_cache();
    if (previous == BargeState::Candidate) g_barge_cooldown_until_us = esp_timer_get_time() + 1000000;
    if (restore_playback && previous == BargeState::Candidate) {
        g_echo_floor_rms = (g_echo_floor_rms * 2 + g_candidate_clean_rms) / 3;
        send_barge_message("resume");
        ESP_LOGI(TAG, "barge_state resume reason=voice_timeout duration_ms=%lu", static_cast<unsigned long>(started_us ? (esp_timer_get_time() - started_us) / 1000 : 0));
    }
}

static bool near_voice(const AudioCaptureMetrics &metrics) {
    if (g_barge_state.load() == BargeState::Candidate) {
        const uint32_t noise = g_clean_rms_baseline > 300 ? 300 : g_clean_rms_baseline;
        return metrics.voice_active && metrics.clean_rms >= (noise * 3 > 200 ? noise * 3 : 200);
    }
    if (!metrics.voice_active) {
        if (!g_speaking && metrics.clean_rms < 1000) g_clean_rms_baseline = (g_clean_rms_baseline * 9 + metrics.clean_rms) / 10;
        return false;
    }
    const uint32_t baseline = g_clean_rms_baseline ? g_clean_rms_baseline : 1;
    if (g_speaking && metrics.reference_rms >= 20 && fabsf(metrics.echo_correlation) >= 0.60f) {
        if (metrics.clean_rms > g_echo_floor_rms) g_echo_floor_rms = (g_echo_floor_rms + metrics.clean_rms) / 2;
        return false;
    }
    // 候选只做可恢复的暂停；不能要求人声盖过回声峰值，否则阈值可能超过 PCM16。
    // 永久取消仍须通过暂停后的持续近端语音确认。
    const bool above_baseline = metrics.clean_rms >= (baseline * 5 / 2 > 150 ? baseline * 5 / 2 : 150);
    const bool correlation_support = fabsf(metrics.echo_correlation) < 0.60f || metrics.reference_rms < 20;
    return above_baseline && correlation_support;
}

static void start_barge_candidate() {
    if (g_test_disable_barge || !g_speaking || !g_playback_id[0] || g_barge_state.load() != BargeState::None || esp_timer_get_time() < g_barge_cooldown_until_us) return;
    const uint32_t candidate_id = g_next_candidate_id.fetch_add(1) + 1;
    g_barge_candidate_id = candidate_id;
    g_barge_started_us = esp_timer_get_time();
    g_barge_voice_us = 0;
    g_barge_voice_gap_us = 0;
    g_candidate_peak_rms = 0;
    g_barge_confirmed_ack = false;
    g_barge_resume_ack = false;
    g_barge_state = BargeState::Candidate;
    if (audio_playback_pause(true) != ESP_OK) {
        g_barge_state = BargeState::None;
        ESP_LOGW(TAG, "barge_state candidate pause_failed");
        return;
    }
    send_barge_message("candidate");
    ESP_LOGI(TAG, "barge_state candidate candidate_id=%lu playback_id=%s duration_ms=0", (unsigned long)candidate_id, g_playback_id);
}

static void confirm_barge() {
    if (g_barge_state.load() != BargeState::Candidate) return;
    g_barge_state = BargeState::Confirmed;
    g_drop_old_audio = true;
    ++g_generation;
    g_speaking = false;
    g_response_finished = false;
    if (g_audio_queue) xQueueReset(g_audio_queue);
    send_barge_message("confirmed");
    const uint32_t duration_ms = g_barge_started_us ? static_cast<uint32_t>((esp_timer_get_time() - g_barge_started_us) / 1000) : 0;
    ESP_LOGI(TAG, "barge_state confirmed candidate_id=%lu duration_ms=%lu voice_ms=%lu", (unsigned long)g_barge_candidate_id.load(), (unsigned long)duration_ms, (unsigned long)(g_barge_voice_us / 1000));
}



// 只为到期计时、到期提醒和可见失败发声；普通对话状态不触发。
static int16_t *g_chime_pcm;
static size_t g_chime_samples;
static std::atomic<int64_t> g_pending_cues[3]{};
static CueGate g_cue_gate;

static void chime_task(void *) {
    (void)audio_set_notification_output(true);
    for (size_t offset = 0; offset < g_chime_samples; offset += 256) {
        if (g_speaking || g_listening || g_auto_session || g_dnd_active || ota_update_busy()) break;
        const size_t samples = g_chime_samples - offset < 256 ? g_chime_samples - offset : 256;
        if (audio_play_pcm24(reinterpret_cast<uint8_t *>(g_chime_pcm + offset), samples * sizeof(int16_t)) != ESP_OK) { ESP_LOGW(TAG, "notification_cue_write_failed"); break; }
    }
    const int64_t ended = esp_timer_get_time();
    g_playback_end_us = ended;
    g_chime_echo_until_us = ended + 300000;
    g_wake_reset_pending = true;
    g_chime_playing = false;
    (void)audio_set_notification_output(false);
    vTaskDelete(nullptr);
}

static bool start_cue(NotificationCue cue) {
    if (g_speaking || g_listening || g_auto_session || g_dnd_active || ota_update_busy()) return false;
    bool idle = false;
    if (!g_chime_playing.compare_exchange_strong(idle, true)) return false;
    if (!g_chime_pcm) g_chime_pcm = static_cast<int16_t *>(heap_caps_malloc(kCueMaxSamples * sizeof(int16_t), board_profile::working_memory_caps));
    if (!g_chime_pcm) { g_chime_playing = false; ESP_LOGW(TAG, "notification_cue_allocation_failed"); return false; }
    const CueSpec &spec = kCueSpecs[static_cast<uint8_t>(cue)];
    size_t index = 0;
    for (uint8_t note = 0; note < spec.note_count; ++note) {
        const uint32_t samples = spec.notes[note].duration_ms * (kCueRate / 1000);
        for (uint32_t position = 0; position < samples; ++position, ++index) {
            const uint32_t remaining = samples - position;
            const float attack = position < 192 ? static_cast<float>(position) / 192.0f : 1.0f;
            const float release = remaining < 672 ? static_cast<float>(remaining) / 672.0f : 1.0f;
            const float t = static_cast<float>(position) / kCueRate;
            g_chime_pcm[index] = static_cast<int16_t>(kCueAmplitude * attack * release * sinf(6.28318530718f * spec.notes[note].frequency_hz * t));
        }
        const uint32_t gap = spec.notes[note].gap_ms * (kCueRate / 1000);
        for (uint32_t position = 0; position < gap; ++position) g_chime_pcm[index++] = 0;
    }
    g_chime_samples = index;
    if (xTaskCreateWithCaps(chime_task, "notification_cue", 8192, nullptr, 4, nullptr, board_profile::working_memory_caps) != pdPASS) { g_chime_playing = false; ESP_LOGW(TAG, "notification_cue_task_failed"); return false; }
    ESP_LOGI(TAG, "notification_cue_started kind=%u samples=%u", static_cast<unsigned>(cue), static_cast<unsigned>(index));
    return true;
}

static void service_pending_cues() {
    const int64_t now = esp_timer_get_time();
    for (uint8_t index = 0; index < 3; ++index) {
        const int64_t queued = g_pending_cues[index].load();
        if (!queued) continue;
        if (now - queued > static_cast<int64_t>(kCueSpecs[index].expires_ms) * 1000) { g_pending_cues[index] = 0; continue; }
        if (g_chime_playing || g_speaking || g_listening || g_auto_session || g_dnd_active || ota_update_busy()) return;
        const auto cue = static_cast<NotificationCue>(index);
        if (!g_cue_gate.allows(cue, now / 1000)) continue;
        g_pending_cues[index] = 0;
        if (start_cue(cue)) g_cue_gate.record(cue, now / 1000);
        return;
    }
}

struct ScreenAction { char action[17]; char reference_id[81]; char revision[201]; };
static QueueHandle_t g_screen_action_queue;

bool control_protocol_queue_screen_action(const char *action, const char *reference_id, const char *revision) {
    if (!action || !reference_id || !revision || strlen(action) > 16 || strlen(reference_id) > 80 || strlen(revision) > 200) return false;
    const int cue_index = strcmp(action, "cue_timer") == 0 ? 0 : strcmp(action, "cue_reminder") == 0 ? 1 : strcmp(action, "cue_error") == 0 ? 2 : -1;
    if (cue_index >= 0) { int64_t empty = 0; g_pending_cues[cue_index].compare_exchange_strong(empty, esp_timer_get_time()); return true; }
    if (!g_screen_action_queue) return false;
    ScreenAction message = {}; snprintf(message.action, sizeof(message.action), "%s", action); snprintf(message.reference_id, sizeof(message.reference_id), "%s", reference_id); snprintf(message.revision, sizeof(message.revision), "%s", revision);
    return xQueueSend(g_screen_action_queue, &message, 0) == pdTRUE;
}

static void screen_action_task(void *) {
    ScreenAction action;
    while (true) {
        service_media_receipts();
        service_pending_cues();
        if (xQueueReceive(g_screen_action_queue, &action, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (!g_ws || !esp_websocket_client_is_connected(g_ws)) continue;
        if (strcmp(action.action, "reminder_ack") == 0) { cJSON *ack = cJSON_CreateObject(); cJSON_AddStringToObject(ack, "type", "notification_ack"); cJSON_AddStringToObject(ack, "id", action.reference_id); cJSON_AddStringToObject(ack, "lease_token", action.revision); send_json(ack); continue; }
        if (strcmp(action.action, "receipt") == 0) { cJSON *ack = cJSON_CreateObject(); cJSON_AddStringToObject(ack, "type", "screen_card_ack"); cJSON_AddStringToObject(ack, "card_id", action.reference_id); cJSON_AddStringToObject(ack, "status", action.revision); send_json(ack); continue; }
        cJSON *message = cJSON_CreateObject(); cJSON_AddStringToObject(message, "type", "screen_action"); cJSON_AddStringToObject(message, "action", action.action); cJSON_AddStringToObject(message, "reference_id", action.reference_id); cJSON_AddStringToObject(message, "revision", action.revision); send_json(message);
    }
}

static bool accept_manual_input_ready(const char *manual_id) {
    const uint32_t token = g_manual_owner_request.load();
    if (!manual_id || !token || !g_manual_active.load() || !g_manual_start_sent.load() || !g_manual_input_ready_supported.load() || !g_wake_tx_enabled.load() || g_wake_upload_blocked.load() || g_wake_upload_release_pending.load()) return false;
    char expected_id[32];
    snprintf(expected_id, sizeof(expected_id), "manual-%lu", static_cast<unsigned long>(token));
    if (strcmp(manual_id, expected_id) != 0) return false;
    // 回调仅发布数值token；owner核对当前代后放行，迟到回调不能写新代tx_ready。
    g_manual_ready_ack_token = token;
    return true;
}

static void handle_text(const char *text, size_t length) {
    cJSON *root = cJSON_ParseWithLength(text, length); if (!root) return;
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) { cJSON_Delete(root); return; }
    if (strcmp(type->valuestring, "hello") == 0) {
        const cJSON *capabilities = cJSON_GetObjectItem(root, "capabilities");
        g_wake_clip_supported = !board_profile::manual_audio_only && cJSON_IsObject(capabilities) && cJSON_IsTrue(cJSON_GetObjectItem(capabilities, "wake_clip_v1"));
        g_wake_input_ready_supported = !board_profile::manual_audio_only && cJSON_IsObject(capabilities) && cJSON_IsTrue(cJSON_GetObjectItem(capabilities, "wake_input_ready_v1"));
        g_manual_input_ready_supported = cJSON_IsObject(capabilities) && cJSON_IsTrue(cJSON_GetObjectItem(capabilities, "manual_input_ready_v1"));
        const cJSON *audio = cJSON_GetObjectItem(root, "audio_params");
        if (cJSON_IsObject(audio)) { const cJSON *rate = cJSON_GetObjectItem(audio, "sample_rate"); if (cJSON_IsNumber(rate) && rate->valueint != 24000) ESP_LOGW(TAG, "服务端输出采样率为 %d，当前播放链固定 24000", rate->valueint); }
        cJSON *memory = cJSON_CreateObject(); cJSON_AddStringToObject(memory, "type", "memory_request"); cJSON_AddStringToObject(memory, "request_id", "boot"); send_json(memory);
    } else if (strcmp(type->valuestring, "conversation_mode") == 0) {
        const cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
        if (!cJSON_IsString(session_id) || !g_voice_session_id[0] || strcmp(session_id->valuestring, g_voice_session_id) != 0) { cJSON_Delete(root); return; }
        const bool aec_ready = audio_echo_cancellation_ready();
        const bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(root, "natural_barge_in")) && g_auto_session && !g_manual_active && aec_ready;
        // [人工决策-2026-11-09 13:12:23] 牛牛要求自然插话；端云确认后持续上传AEC输出，旧本地强打断仍关闭。
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "type", "conversation_mode_ack");
        cJSON_AddStringToObject(ack, "session_id", g_voice_session_id);
        cJSON_AddBoolToObject(ack, "natural_barge_in", enabled);
        cJSON_AddBoolToObject(ack, "aec_ready", aec_ready);
        g_natural_barge_in = send_json(ack) && enabled;
        ESP_LOGI(TAG, "natural_barge_in enabled=%d aec_ready=%d", enabled, aec_ready);
    } else if (strcmp(type->valuestring, "manual_input_ready") == 0) {
        const cJSON *manual_id = cJSON_GetObjectItem(root, "manual_id");
        if (cJSON_IsString(manual_id)) (void)accept_manual_input_ready(manual_id->valuestring);
    } else if (strcmp(type->valuestring, "wake_input_ready") == 0) {
        const cJSON *wake_id = cJSON_GetObjectItem(root, "wake_id");
        if (g_wake_input_ready_supported.load() && g_wake_tx_enabled.load() && !g_wake_upload_blocked.load() && !g_wake_upload_release_pending.load() && cJSON_IsString(wake_id) && strcmp(wake_id->valuestring, g_wake_id) == 0) { g_wake_input_ready_us = esp_timer_get_time(); g_wake_tx_ready = true; }
    } else if (strcmp(type->valuestring, "media_start") == 0 || strcmp(type->valuestring, "media_pause") == 0 || strcmp(type->valuestring, "media_stop") == 0 || strcmp(type->valuestring, "media_end") == 0) {
        if (strcmp(type->valuestring, "media_start") == 0) { g_navigation_id[0] = '\0'; display_ui_nav_icon(0); }
        receive_media_command(root, type->valuestring);
    } else if (strcmp(type->valuestring, "device_preferences") == 0) {
        const cJSON *conversation_volume = cJSON_GetObjectItem(root, "conversation_volume");
        const cJSON *reminder_volume = cJSON_GetObjectItem(root, "reminder_volume");
        const cJSON *conversation_muted = cJSON_GetObjectItem(root, "conversation_muted");
        const cJSON *reminders_muted = cJSON_GetObjectItem(root, "reminders_muted");
        const cJSON *quiet_active = cJSON_GetObjectItem(root, "quiet_active");
        const cJSON *brightness = cJSON_GetObjectItem(root, "effective_brightness");
        const cJSON *night_brightness = cJSON_GetObjectItem(root, "night_brightness");
        const cJSON *night_mode = cJSON_GetObjectItem(root, "night_mode_enabled");
        const cJSON *screen_off = cJSON_GetObjectItem(root, "screen_off_seconds");
        if (cJSON_IsNumber(conversation_volume) && cJSON_IsNumber(reminder_volume) && cJSON_IsBool(conversation_muted) && cJSON_IsBool(reminders_muted) && cJSON_IsBool(quiet_active) && cJSON_IsNumber(brightness) && cJSON_IsNumber(night_brightness) && cJSON_IsBool(night_mode) && cJSON_IsNumber(screen_off) && conversation_volume->valueint >= 0 && conversation_volume->valueint <= 100 && reminder_volume->valueint >= 0 && reminder_volume->valueint <= 100 && brightness->valueint >= 0 && brightness->valueint <= 100 && night_brightness->valueint >= 0 && night_brightness->valueint <= 100 && screen_off->valueint >= 0 && screen_off->valueint <= 86400) {
            const esp_err_t audio_result = audio_set_output_preferences(static_cast<uint8_t>(conversation_volume->valueint), static_cast<uint8_t>(reminder_volume->valueint), cJSON_IsTrue(conversation_muted), cJSON_IsTrue(reminders_muted));
            const esp_err_t display_result = display_ui_apply_preferences(static_cast<uint8_t>(brightness->valueint), static_cast<uint8_t>(night_brightness->valueint), cJSON_IsTrue(night_mode), static_cast<uint32_t>(screen_off->valueint));
            g_dnd_active = cJSON_IsTrue(quiet_active);
            cJSON *ack = cJSON_CreateObject(); cJSON_AddStringToObject(ack, "type", "device_preferences_applied"); cJSON_AddBoolToObject(ack, "audio", audio_result == ESP_OK); cJSON_AddBoolToObject(ack, "display", display_result == ESP_OK); cJSON_AddBoolToObject(ack, "battery_known", false); send_json(ack);
        }
    } else if (strcmp(type->valuestring, "ota") == 0) {
        const cJSON *ota_url = cJSON_GetObjectItem(root, "url");
        const cJSON *ota_sha = cJSON_GetObjectItem(root, "sha256");
        if (cJSON_IsString(ota_url) && strlen(ota_url->valuestring) < 200) {
            ESP_LOGI(TAG, "ota_command url=%s", ota_url->valuestring);
            g_navigation_id[0] = '\0';
            display_ui_nav_icon(0);
            ota_update_start(ota_url->valuestring, cJSON_IsString(ota_sha) ? ota_sha->valuestring : "", g_device_token);
        }
    } else if (strcmp(type->valuestring, "stt") == 0 || strcmp(type->valuestring, "sentence_start") == 0) {
        const cJSON *text = cJSON_GetObjectItem(root, "text"); if (cJSON_IsString(text) && !g_speaking) display_ui_state(g_requested_ui.load(), text->valuestring);
    } else if (strcmp(type->valuestring, "alert") == 0) {
        const cJSON *message = cJSON_GetObjectItem(root, "message"); display_ui_state(UiState::Error, cJSON_IsString(message) ? message->valuestring : "服务提醒");
    } else if (strcmp(type->valuestring, "diagnostics") == 0) {
        const cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
        g_diagnostics = cJSON_IsTrue(enabled);
        g_test_disable_barge = g_diagnostics && cJSON_IsTrue(cJSON_GetObjectItem(root, "disable_barge"));
        if (g_diagnostics && cJSON_IsTrue(cJSON_GetObjectItem(root, "capture_audio"))) g_audio_diagnostic_requested = true;
        // 已认证的本地检查服务可跳过唤醒入口，直接验证音频与插话链路。
        if (g_diagnostics && cJSON_IsTrue(cJSON_GetObjectItem(root, "test_listening")) && !g_speaking) {
            g_auto_session = true; g_listening = true; g_cloud_ready = true;
            send_control("device_state", "listening");
        }
    } else if (strcmp(type->valuestring, "navigation") == 0) {
        const cJSON *state = cJSON_GetObjectItem(root, "state");
        const cJSON *identity = cJSON_GetObjectItem(root, "navigation_id");
        const cJSON *nav_icon = cJSON_GetObjectItem(root, "nav_icon");
        if (cJSON_IsString(state) && cJSON_IsString(identity) && identity->valuestring[0] && strlen(identity->valuestring) < sizeof(g_navigation_id)) {
            if (strcmp(state->valuestring, "step") == 0 && cJSON_IsString(nav_icon) && !g_listening && !g_media_active && !g_speaking && !ota_update_busy()) {
                const char *name = nav_icon->valuestring;
                const uint8_t icon = strcmp(name, "straight") == 0 ? 1 : strcmp(name, "left") == 0 ? 2 : strcmp(name, "right") == 0 ? 3 : strcmp(name, "uturn") == 0 ? 4 : strcmp(name, "flag") == 0 ? 5 : 0;
                if (icon != 0) { snprintf(g_navigation_id, sizeof(g_navigation_id), "%s", identity->valuestring); display_ui_nav_icon(icon); }
            } else if (strcmp(state->valuestring, "end") == 0 && strcmp(g_navigation_id, identity->valuestring) == 0) {
                g_navigation_id[0] = '\0';
                display_ui_nav_icon(0);
            }
        }
    } else if (strcmp(type->valuestring, "tts") == 0 || strcmp(type->valuestring, "tts_start") == 0) {
        if (g_media_active) { cJSON_Delete(root); return; }
        const cJSON *state = cJSON_GetObjectItem(root, "state");
        const cJSON *incoming_playback = cJSON_GetObjectItem(root, "playback_id");
        const bool start = cJSON_IsString(state) && strcmp(state->valuestring, "start") == 0;
        if (!start && cJSON_IsString(incoming_playback) && g_playback_id[0] && strcmp(incoming_playback->valuestring, g_playback_id) != 0) { cJSON_Delete(root); return; }
        if (cJSON_IsString(state) && strcmp(state->valuestring, "stop") == 0) {
            if (g_barge_state.load() == BargeState::Confirmed) { cJSON_Delete(root); return; }
            g_response_finished = true;
            AudioChunk end = {}; end.generation = g_generation;
            if (xQueueSend(g_audio_queue, &end, 0) != pdTRUE) { g_navigation_id[0] = '\0'; display_ui_nav_icon(0); ++g_generation; note_playback_stopped(); xQueueReset(g_audio_queue); g_abort_requested = true; }
        }
        else {
            const cJSON *text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(state) && strcmp(state->valuestring, "start") == 0) {
                g_drop_old_audio = false;
                (void)audio_playback_pause(false);
                g_response_finished = false;
                g_tts_received_us = esp_timer_get_time();
                g_first_pcm_received_us = 0;
                g_barge_played_samples = 0;
                const cJSON *playback_id = cJSON_GetObjectItem(root, "playback_id");
                if (cJSON_IsString(playback_id) && playback_id->valuestring[0]) {
                    snprintf(g_playback_id, sizeof(g_playback_id), "%s", playback_id->valuestring);
                } else {
                    g_playback_id[0] = '\0';
                }
                if (!g_speaking) g_playback_started_us = esp_timer_get_time();
                g_speaking = true;
                g_wake_reset_pending = true;
                // 导航播报随句携带图标；图标跨句保持，直到显式 navigation end 或新的会话清理。
                const cJSON *nav_icon = cJSON_GetObjectItem(root, "nav_icon");
                if (cJSON_IsString(nav_icon)) {
                    const char *name = nav_icon->valuestring;
                    const uint8_t icon = strcmp(name, "straight") == 0 ? 1 : strcmp(name, "left") == 0 ? 2 : strcmp(name, "right") == 0 ? 3 : strcmp(name, "uturn") == 0 ? 4 : strcmp(name, "flag") == 0 ? 5 : 0;
                    if (icon != 0) display_ui_nav_icon(icon);
                    else { g_navigation_id[0] = '\0'; display_ui_nav_icon(0); }
                } else {
                    g_navigation_id[0] = '\0';
                    display_ui_nav_icon(0);
                }
            }
            if (cJSON_IsString(text)) display_ui_state(g_speaking ? UiState::Speaking : g_requested_ui.load(), text->valuestring);
        }
    } else if (strcmp(type->valuestring, "barge_in_ack") == 0) {
        const cJSON *phase = cJSON_GetObjectItem(root, "phase");
        if (cJSON_IsString(phase) && same_barge_identity(root)) {
            ESP_LOGI(TAG, "barge_ack phase=%s candidate_id=%lu", phase->valuestring, (unsigned long)g_barge_candidate_id.load());
            if (strcmp(phase->valuestring, "confirmed") == 0 && g_barge_state.load() == BargeState::Confirmed) {
                g_barge_confirmed_ack = true;
            } else if (strcmp(phase->valuestring, "resume") == 0 && g_barge_state.load() == BargeState::Candidate) {
                g_barge_resume_ack = true;
            }
        }
    } else if (strcmp(type->valuestring, "tts_stop") == 0 || strcmp(type->valuestring, "interrupt") == 0) {
        if (g_media_active) { cJSON_Delete(root); return; }
        const cJSON *playback_id = cJSON_GetObjectItem(root, "playback_id");
        if (cJSON_IsString(playback_id) && g_playback_id[0] && strcmp(playback_id->valuestring, g_playback_id) != 0) { cJSON_Delete(root); return; }
        const bool matching_barge = cJSON_IsString(playback_id) && strcmp(playback_id->valuestring, g_playback_id) == 0;
        if (g_barge_state.load() == BargeState::Confirmed && matching_barge) {
            ESP_LOGI(TAG, "barge interrupt acknowledged playback_id=%s", g_playback_id);
        } else {
            reset_barge_state(false);
            g_drop_old_audio = true;
            g_navigation_id[0] = '\0';
            ++g_generation; note_playback_stopped(); g_requested_ui = UiState::Listening; display_ui_audio_level(0); display_ui_nav_icon(0); g_abort_requested = true; if (g_audio_queue) xQueueReset(g_audio_queue); display_ui_state(UiState::Listening); send_control("device_state", "listening");
        }
    } else if (strcmp(type->valuestring, "custom") == 0) {
        if (g_media_active) { cJSON_Delete(root); return; }
        const cJSON *payload = cJSON_GetObjectItem(root, "payload"); const cJSON *state = payload ? cJSON_GetObjectItem(payload, "state") : nullptr;
        if (cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "connecting") == 0) {
                g_natural_barge_in = false;
                const cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
                g_voice_session_id[0] = '\0';
                if (cJSON_IsString(session_id) && strlen(session_id->valuestring) < sizeof(g_voice_session_id)) snprintf(g_voice_session_id, sizeof(g_voice_session_id), "%s", session_id->valuestring);
            }
            if (strcmp(state->valuestring, "idle") == 0) { g_natural_barge_in = false; g_voice_session_id[0] = '\0'; }
            const cJSON *detail = cJSON_GetObjectItem(payload, "detail");
            UiState ui = UiState::Idle;
            if (strcmp(state->valuestring, "idle") == 0) { reset_barge_state(false); g_navigation_id[0] = '\0'; display_ui_nav_icon(0); ++g_generation; note_playback_stopped(); g_abort_requested = true; if (g_audio_queue) xQueueReset(g_audio_queue); g_listening = false; g_cloud_ready = false; g_auto_session = false; g_wake_upload_pending = false; g_wake_reset_pending = true; confirm_wake_audio_release(); }
            else if (strcmp(state->valuestring, "listening") == 0) { if (!accept_wake_listening()) { cJSON_Delete(root); return; } ui = UiState::Listening; }
            else if (strcmp(state->valuestring, "speaking") == 0) ui = UiState::Speaking;
            else if (strcmp(state->valuestring, "thinking") == 0) ui = UiState::Thinking;
            else if (strcmp(state->valuestring, "executing") == 0 || strcmp(state->valuestring, "searching") == 0) ui = UiState::Executing;
            else if (strcmp(state->valuestring, "connecting") == 0) ui = UiState::Connecting;
            else if (strcmp(state->valuestring, "error") == 0) ui = UiState::Error;
            else if (strcmp(state->valuestring, "alert") == 0 || strcmp(state->valuestring, "waiting_confirmation") == 0 || strcmp(state->valuestring, "waiting_authorization") == 0) ui = UiState::Alert;
            else if (strcmp(state->valuestring, "updating") == 0) ui = UiState::Updating;
            else { ui = UiState::Alert; ESP_LOGW(TAG, "unsupported_ui_state"); }
            g_requested_ui = ui;
            const bool defer = g_speaking && (ui == UiState::Listening || ui == UiState::Thinking || ui == UiState::Executing);
            ESP_LOGI(TAG, "ui_request state=%s playback_active=%d deferred=%d", state->valuestring, g_speaking.load(), defer);
            if (!defer) display_ui_state(ui, cJSON_IsString(detail) ? detail->valuestring : nullptr);
        }
    } else if (strcmp(type->valuestring, "notification") == 0) {
        const cJSON *n = cJSON_GetObjectItem(root, "notification");
        if (cJSON_IsObject(n)) {
            const cJSON *title = cJSON_GetObjectItem(n, "title"); const cJSON *identity = cJSON_GetObjectItem(n, "id"); const cJSON *lease = cJSON_GetObjectItem(n, "lease_token");
            if (cJSON_IsString(title) && cJSON_IsString(identity) && identity->valuestring[0] && strlen(identity->valuestring) <= 64 && cJSON_IsString(lease) && lease->valuestring[0] && strlen(lease->valuestring) <= 200) {
                ScreenCardData card = {}; snprintf(card.kind, sizeof(card.kind), "reminder"); snprintf(card.title, sizeof(card.title), "%s", title->valuestring); snprintf(card.detail, sizeof(card.detail), "已显示提醒，不代表待办已完成");
                snprintf(card.card_id, sizeof(card.card_id), "reminder-%.64s", identity->valuestring); snprintf(card.reference_id, sizeof(card.reference_id), "%s", identity->valuestring); snprintf(card.notification_lease, sizeof(card.notification_lease), "%s", lease->valuestring);
                card.ttl_s = 600; display_ui_screen_card(card);
            }
        }
    } else if (strcmp(type->valuestring, "screen_card") == 0) {
        ScreenCardData card = {};
        struct Field { const char *key; char *value; size_t capacity; };
        Field fields[] = {{"card_id", card.card_id, sizeof(card.card_id)}, {"kind", card.kind, sizeof(card.kind)}, {"title", card.title, sizeof(card.title)}, {"detail", card.detail, sizeof(card.detail)}, {"direction", card.direction, sizeof(card.direction)}, {"status", card.status, sizeof(card.status)}, {"reference_id", card.reference_id, sizeof(card.reference_id)}, {"reference_revision", card.reference_revision, sizeof(card.reference_revision)}};
        bool valid = true;
        for (auto &field : fields) { const cJSON *value = cJSON_GetObjectItem(root, field.key); if (!cJSON_IsString(value) || strlen(value->valuestring) >= field.capacity) { valid = false; break; } snprintf(field.value, field.capacity, "%s", value->valuestring); }
        const char *keys[] = {"remaining_s", "distance_m", "duration_s", "ttl_s"};
        int32_t *numbers[] = {&card.remaining_s, &card.distance_m, &card.duration_s};
        for (int index = 0; valid && index < 4; ++index) { const cJSON *value = cJSON_GetObjectItem(root, keys[index]); const int maximum = index == 1 ? 10000000 : 86400; if (!cJSON_IsNumber(value) || value->valuedouble < 0 || value->valuedouble > maximum || value->valuedouble != value->valueint) { valid = false; break; } if (index == 3) card.ttl_s = value->valueint; else *numbers[index] = value->valueint; }
        const bool known_kind = strcmp(card.kind, "timer") == 0 || strcmp(card.kind, "weather") == 0 || strcmp(card.kind, "today") == 0 || strcmp(card.kind, "home") == 0 || strcmp(card.kind, "task") == 0 || strcmp(card.kind, "route") == 0 || strcmp(card.kind, "power") == 0;
        const bool known_timer_status = strcmp(card.kind, "timer") != 0 || strcmp(card.status, "running") == 0 || strcmp(card.status, "paused") == 0 || strcmp(card.status, "finished") == 0 || strcmp(card.status, "cancelled") == 0;
        if (valid && known_kind && known_timer_status && card.card_id[0] && card.ttl_s > 0) {
            const char *status = display_ui_screen_card(card);
            cJSON *ack = cJSON_CreateObject(); cJSON_AddStringToObject(ack, "type", "screen_card_ack"); cJSON_AddStringToObject(ack, "card_id", card.card_id); cJSON_AddStringToObject(ack, "status", status); send_json(ack);
        }
    } else if (strcmp(type->valuestring, "reminder_state") == 0) {
        const cJSON *reference = cJSON_GetObjectItem(root, "reference_id"); const cJSON *revision = cJSON_GetObjectItem(root, "revision");
        if (cJSON_IsString(reference) && cJSON_IsString(revision) && strlen(reference->valuestring) <= 80 && strlen(revision->valuestring) <= 64) display_ui_reminder_revision(reference->valuestring, revision->valuestring);
    } else if (strcmp(type->valuestring, "screen_action_result") == 0) {
        const cJSON *reference = cJSON_GetObjectItem(root, "reference_id"); const cJSON *status = cJSON_GetObjectItem(root, "status");
        if (cJSON_IsString(reference) && cJSON_IsString(status)) display_ui_screen_action_result(reference->valuestring, strcmp(status->valuestring, "accepted") == 0);
    } else if (strcmp(type->valuestring, "memory_snapshot") == 0) {
        const cJSON *request_id = cJSON_GetObjectItem(root, "request_id");
        const cJSON *items = cJSON_GetObjectItem(root, "items");
        if (cJSON_IsString(request_id) && cJSON_IsArray(items)) {
            char *snapshot = cJSON_PrintUnformatted(root); if (snapshot) { storage_write_memory_snapshot(snapshot); free(snapshot); }
        }
    }
    cJSON_Delete(root);
}

static void ws_event(void *, esp_event_base_t, int32_t event_id, void *event_data) {
    auto *event = static_cast<esp_websocket_event_data_t *>(event_data);
    if (event_id == WEBSOCKET_EVENT_CONNECTED) { g_manual_input_ready_supported = false; display_ui_screen_link(true); g_last_downlink_us = esp_timer_get_time(); (void)esp_ota_mark_app_valid_cancel_rollback(); send_hello(); send_control("device_state", "idle"); }
    else if (event_id == WEBSOCKET_EVENT_DATA) {
        if (event->data_len > 0 || event->payload_len > 0) g_last_downlink_us = esp_timer_get_time();
        if (g_reject_text_fragment && event->op_code == 0x0) { if (event->fin) g_reject_text_fragment = false; return; }
        if ((event->op_code == 0x1 || (event->op_code == 0x0 && g_text_fragment_active)) && event->data_ptr && event->data_len) {
            if (event->data_len > static_cast<int>(sizeof(g_text_fragments) - g_text_fragment_length - 1)) { ESP_LOGW(TAG, "文本帧超过6144字节，拒绝解析"); g_text_fragment_length = 0; g_text_fragment_active = false; g_reject_text_fragment = !event->fin; return; }
            const size_t copy_length = event->data_len;
            memcpy(g_text_fragments + g_text_fragment_length, event->data_ptr, copy_length); g_text_fragment_length += copy_length; g_text_fragment_active = true;
            if (event->fin && event->payload_offset + event->data_len == event->payload_len) { g_text_fragments[g_text_fragment_length] = '\0'; handle_text(g_text_fragments, g_text_fragment_length); g_text_fragment_length = 0; g_text_fragment_active = false; }
        } else if ((event->op_code == 0x2 || event->op_code == 0x0) && event->data_ptr && event->data_len && g_audio_queue && !g_drop_old_audio) {
            if (!g_first_pcm_received_us.load()) g_first_pcm_received_us = esp_timer_get_time();
            // 候选阶段保留已在途音频，暂停播放而非丢弃恢复时的首帧。
            if (g_barge_state.load() == BargeState::Confirmed) return;
            if (g_media_active && ((event->data_len % sizeof(int16_t)) != 0 || event->payload_len > 960 || event->payload_len <= 0)) {
                MediaCommand command = {}; command.kind = MediaCommandKind::Error; snprintf(command.playback_id, sizeof(command.playback_id), "%s", g_media_id); snprintf(command.error, sizeof(command.error), "invalid_media_frame");
                if (!queue_media_command(command)) g_media_error_pending = true;
                g_drop_old_audio = true; return;
            }
            for (int offset = 0; offset < event->data_len; offset += sizeof(g_incoming_chunk.data)) {
                g_incoming_chunk = {}; g_incoming_chunk.generation = g_generation; g_incoming_chunk.length = (event->data_len - offset) > static_cast<int>(sizeof(g_incoming_chunk.data)) ? sizeof(g_incoming_chunk.data) : event->data_len - offset;
                g_incoming_chunk.media = g_media_active.load();
                memcpy(g_incoming_chunk.data, event->data_ptr + offset, g_incoming_chunk.length);
                if (xQueueSend(g_audio_queue, &g_incoming_chunk, 0) != pdTRUE) {
                    ++g_queue_overflows;
                    ESP_LOGE(TAG, "音频缓冲已满，停止本轮播放");
                    if (g_media_active) { MediaCommand command = {}; command.kind = MediaCommandKind::Error; snprintf(command.playback_id, sizeof(command.playback_id), "%s", g_media_id); snprintf(command.error, sizeof(command.error), "audio_queue_full"); if (!queue_media_command(command)) g_media_error_pending = true; g_drop_old_audio = true; break; }
                    g_navigation_id[0] = '\0'; display_ui_nav_icon(0);
                    ++g_generation; note_playback_stopped(); xQueueReset(g_audio_queue); g_abort_requested = true;
                    break;
                }
            }
        }
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED) { g_natural_barge_in = false; g_voice_session_id[0] = '\0'; g_navigation_id[0] = '\0'; display_ui_nav_icon(0); display_ui_screen_link(false); reset_barge_state(false); ++g_generation; g_media_active = false; g_media_wake_pending = false; if (g_media_commands) xQueueReset(g_media_commands); g_listening = false; note_playback_stopped(); g_cloud_ready = false; g_auto_session = false; g_wake_upload_pending = false; g_wake_reset_pending = true; confirm_wake_audio_release(); g_abort_requested = true; if (g_audio_queue) xQueueReset(g_audio_queue); g_text_fragment_length = 0; g_text_fragment_active = false; g_reject_text_fragment = false; display_ui_state(UiState::Connecting, "连接断开，正在重连"); }
}

static void cache_wake_audio(const uint8_t *data, size_t length) {
    if (!data || !length || g_wake_upload_blocked.load()) return;
    if (g_wake_tx_enabled.load()) { if (!queue_wake_audio(data, length)) abort_wake_audio(); return; }
    if (!g_wake_prefix_frozen && g_wake_prefix_cache) {
        const size_t first = length < WAKE_PREFIX_BYTES - g_wake_prefix_write ? length : WAKE_PREFIX_BYTES - g_wake_prefix_write;
        memcpy(g_wake_prefix_cache + g_wake_prefix_write, data, first);
        if (length > first) memcpy(g_wake_prefix_cache, data + first, length - first);
        g_wake_prefix_write = (g_wake_prefix_write + length) % WAKE_PREFIX_BYTES;
        g_wake_prefix_count = g_wake_prefix_count + length < WAKE_PREFIX_BYTES ? g_wake_prefix_count + length : WAKE_PREFIX_BYTES;
    }
    if (!g_wake_cache) return;
    if (g_wake_clip_active && g_wake_cache_count + length > WAKE_CACHE_BYTES) {
        g_wake_cache_overflow = true;
        return;
    }
    const size_t first = length < WAKE_CACHE_BYTES - g_wake_cache_write ? length : WAKE_CACHE_BYTES - g_wake_cache_write;
    memcpy(g_wake_cache + g_wake_cache_write, data, first);
    if (length > first) memcpy(g_wake_cache, data + first, length - first);
    g_wake_cache_write = (g_wake_cache_write + length) % WAKE_CACHE_BYTES;
    g_wake_cache_count = g_wake_cache_count + length < WAKE_CACHE_BYTES ? g_wake_cache_count + length : WAKE_CACHE_BYTES;
}

static void note_wake_boundary_audio(size_t bytes, const AudioCaptureFrame &frame) {
    if (g_wake_prefix_frozen || g_wake_tx_enabled.load() || g_wake_upload_blocked.load()) return;
    const uint32_t samples = bytes / sizeof(int16_t);
    const bool continuous = frame.continuous && g_wake_boundary_seen && frame.generation == g_wake_boundary_generation && frame.sample_index == g_wake_boundary_expected_sample;
    if (!continuous) {
        g_wake_boundary_quiet_samples = 0;
        g_wake_boundary_reset = false;
        wake_word_reset();
    }
    g_wake_boundary_seen = true;
    g_wake_boundary_generation = frame.generation;
    g_wake_boundary_expected_sample = frame.sample_index + samples;
    if (!frame.vad_valid || frame.voice_active) {
        g_wake_boundary_quiet_samples = 0;
        g_wake_boundary_reset = false;
        return;
    }
    // 同序VAD确认500ms静音后，只移除已结束的旧段；保留300ms前导和后续整段人声。
    g_wake_boundary_quiet_samples = g_wake_boundary_quiet_samples + samples < 8000 ? g_wake_boundary_quiet_samples + samples : 8000;
    if (g_wake_boundary_quiet_samples < 8000) return;
    if (g_wake_prefix_count > 9600) g_wake_prefix_count = 9600;
    if (!g_wake_boundary_reset) {
        wake_word_reset();
        g_wake_boundary_reset = true;
        ESP_LOGI(TAG, "wake_audio_quiet_boundary generation=%lu prefix_bytes=%lu", static_cast<unsigned long>(frame.generation), static_cast<unsigned long>(g_wake_prefix_count));
    }
}

static bool send_wake_audio_segment(const uint8_t *data, size_t length) {
    // 桥接单帧上限64KB，前置4秒PCM必须分块上传。
    for (size_t offset = 0; offset < length; offset += 32000) {
        if (g_wake_upload_release_pending.load()) return false;
        if (!g_wake_first_upload_us) g_wake_first_upload_us = esp_timer_get_time();
        const size_t count = length - offset < 32000 ? length - offset : 32000;
        const int sent = esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(data + offset), count, pdMS_TO_TICKS(1000));
        if (sent != static_cast<int>(count)) {
            ESP_LOGW(TAG, "wake_audio_chunk_failed expected=%lu actual=%d wake_id=%s", static_cast<unsigned long>(count), sent, g_wake_id);
            return false;
        }
    }
    return true;
}

static bool upload_wake_audio() {
    if (!g_ws || !esp_websocket_client_is_connected(g_ws)) return false;
    if (g_wake_cache_overflow) return false;
    const size_t prefix_bytes = g_wake_prefix_count;
    const size_t suffix_bytes = g_wake_cache_count;
    if (!prefix_bytes && !suffix_bytes) return true;
    if (prefix_bytes) {
        const size_t start = (g_wake_prefix_write + WAKE_PREFIX_BYTES - prefix_bytes) % WAKE_PREFIX_BYTES;
        const size_t first = prefix_bytes < WAKE_PREFIX_BYTES - start ? prefix_bytes : WAKE_PREFIX_BYTES - start;
        if (!send_wake_audio_segment(g_wake_prefix_cache + start, first)) return false;
        if (first < prefix_bytes && !send_wake_audio_segment(g_wake_prefix_cache, prefix_bytes - first)) return false;
    }
    if (suffix_bytes) {
        const size_t start = (g_wake_cache_write + WAKE_CACHE_BYTES - suffix_bytes) % WAKE_CACHE_BYTES;
        const size_t first = suffix_bytes < WAKE_CACHE_BYTES - start ? suffix_bytes : WAKE_CACHE_BYTES - start;
        if (!send_wake_audio_segment(g_wake_cache + start, first)) return false;
        if (first < suffix_bytes && !send_wake_audio_segment(g_wake_cache, suffix_bytes - first)) return false;
    }
    ESP_LOGI(TAG, "wake_audio_upload prefix_bytes=%lu suffix_bytes=%lu wake_id=%s", static_cast<unsigned long>(prefix_bytes), static_cast<unsigned long>(suffix_bytes), g_wake_id);
    if (g_wake_clip_active) g_wake_clip_sent_bytes += prefix_bytes + suffix_bytes;
    g_wake_prefix_count = 0; g_wake_prefix_write = 0; g_wake_prefix_frozen = false;
    g_wake_cache_count = 0; g_wake_cache_write = 0;
    return true;
}

static void reset_wake_audio_cache() {
    g_wake_tx_enabled = false;
    g_wake_tx_ready = false;
    ++g_wake_tx_generation;
    if (g_wake_tx_queue) xQueueReset(g_wake_tx_queue);
    g_wake_tx_failed = false;
    g_wake_tx_queued_bytes = 0;
    g_wake_tx_sent_bytes = 0;
    g_wake_tx_release_waiting = false;
    g_wake_tx_copying_prefix = false;
    g_wake_prefix_count = 0;
    g_wake_prefix_write = 0;
    g_wake_prefix_frozen = false;
    g_wake_cache_count = 0;
    g_wake_cache_write = 0;
    g_wake_id[0] = '\0';
    g_wake_detected_us = 0;
    g_wake_cloud_ready_us = 0;
    g_wake_input_ready_us = 0;
    g_wake_first_upload_us = 0;
    g_wake_first_upload_done_us = 0;
    g_wake_tx_total_us = g_wake_tx_max_us = 0;
    g_wake_clip_active = false;
    g_wake_cache_overflow = false;
    g_wake_clip_samples = g_wake_clip_quiet_samples = g_wake_clip_sent_bytes = 0;
    g_manual_active = false;
    g_manual_start_sent = false;
    g_manual_end_sent = false;
    g_manual_ready_ack_token = 0;
    g_manual_ended = false;
    g_manual_releasing = false;
    g_manual_pending = {};
    g_manual_capture_seen = false;
    g_manual_id[0] = '\0';
    g_manual_captured_bytes = 0;
    g_wake_boundary_quiet_samples = 0;
    g_wake_boundary_reset = false;
    g_wake_boundary_seen = false;
}

static void set_active_wifi_performance(bool active) {
    if (active == g_wifi_active_requested) return;
    g_wifi_active_requested = active;
    if (active && !g_wifi_original_ps_known) {
        const esp_err_t result = esp_wifi_get_ps(&g_wifi_original_ps);
        if (result != ESP_OK) { ESP_LOGE(TAG, "wifi_power_mode_read_failed error=%d", result); return; }
        g_wifi_original_ps_known = true;
    }
    if (!g_wifi_original_ps_known) return;
    const wifi_ps_type_t requested = active ? WIFI_PS_NONE : g_wifi_original_ps;
    const esp_err_t result = esp_wifi_set_ps(requested);
    if (result != ESP_OK) ESP_LOGE(TAG, "wifi_power_mode_change_failed active=%d mode=%d error=%d", active, static_cast<int>(requested), result);
    else ESP_LOGI(TAG, "wifi_power_mode active=%d mode=%d", active, static_cast<int>(requested));
}

static void service_active_wifi_performance() {
    set_active_wifi_performance(g_listening.load() || g_speaking.load() || g_auto_session.load() || g_cloud_ready.load() || g_media_active.load() || g_media_wake_pending.load() || g_chime_playing.load() || ota_update_busy());
}

static void snapshot_wake_prefix() {
    if (g_wake_upload_blocked.load()) return;
    if (g_wake_upload_release_pending.load()) { g_wake_upload_blocked = true; return; }
    if (g_wake_tx_enabled.load() || g_wake_tx_inflight.load()) { abort_wake_audio(); return; }
    g_wake_detected_us = esp_timer_get_time();
    g_wake_cloud_ready_us = 0;
    g_wake_input_ready_us = 0;
    g_wake_first_upload_us = 0;
    g_wake_first_upload_done_us = 0;
    g_wake_tx_total_us = g_wake_tx_max_us = 0;
    set_active_wifi_performance(true);
    g_wake_prefix_frozen = true;
    g_wake_cache_count = 0;
    g_wake_cache_write = 0;
    g_wake_clip_active = g_wake_clip_supported.load();
    g_wake_cache_overflow = false;
    g_wake_clip_samples = g_wake_clip_quiet_samples = g_wake_clip_sent_bytes = 0;
    snprintf(g_wake_id, sizeof(g_wake_id), "wake-%lu", static_cast<unsigned long>(++g_next_wake_id));
    ESP_LOGI(TAG, "wake_prefix_frozen prefix_bytes=%lu wake_id=%s", static_cast<unsigned long>(g_wake_prefix_count), g_wake_id);
    if (g_wake_clip_active && !g_wake_tx_queue) { ESP_LOGE(TAG, "wake_tx_queue_missing"); abort_wake_audio(); return; }
    if (g_wake_clip_active) {
        g_wake_tx_ready = false;
        ++g_wake_tx_generation;
        xQueueReset(g_wake_tx_queue);
        g_wake_tx_failed = false;
        g_wake_tx_queued_bytes = 0;
        g_wake_tx_sent_bytes = 0;
        g_wake_tx_enabled = true;
        const size_t start = (g_wake_prefix_write + WAKE_PREFIX_BYTES - g_wake_prefix_count) % WAKE_PREFIX_BYTES;
        const size_t first = g_wake_prefix_count < WAKE_PREFIX_BYTES - start ? g_wake_prefix_count : WAKE_PREFIX_BYTES - start;
        g_wake_tx_copying_prefix = true;
        const bool copied = queue_wake_audio(g_wake_prefix_cache + start, first) && (first == g_wake_prefix_count || queue_wake_audio(g_wake_prefix_cache, g_wake_prefix_count - first));
        g_wake_tx_copying_prefix = false;
        if (!copied) abort_wake_audio();
    }
}

static bool service_wake_audio_reset() {
    const bool released = g_wake_upload_release_pending.exchange(false);
    if (released) {
        g_wake_tx_release_waiting = true;
        g_wake_reset_pending = true;
        g_wake_tx_enabled = false;
        g_wake_tx_ready = false;
        ++g_wake_tx_generation;
        if (g_wake_tx_queue) xQueueReset(g_wake_tx_queue);
    }
    if (g_wake_upload_blocked.load() && !g_wake_tx_release_waiting) return false;
    if (!g_wake_reset_pending.exchange(false)) return false;
    // TTS开始的WakeNet复位不能清掉发送队列中的接话；idle/断线回执才释放会话。
    if (g_wake_tx_enabled.load() && !g_wake_tx_release_waiting && (g_listening.load() || g_cloud_ready.load())) { wake_word_reset(); return true; }
    // 远端idle不是本地send已退出的证明；确认发送owner离开写操作后才允许新唤醒。
    if (g_wake_tx_inflight.load()) { g_wake_reset_pending = true; g_wake_upload_blocked = true; return false; }
    wake_word_reset();
    reset_wake_audio_cache();
    g_wake_upload_blocked = false;
    return true;
}

static void abort_wake_audio() {
    g_wake_tx_ready = false;
    g_wake_tx_enabled = false;
    ++g_wake_tx_generation;
    if (g_wake_tx_queue) xQueueReset(g_wake_tx_queue);
    g_wake_upload_blocked = true;
    g_wake_clip_active = false;
    g_listening = false;
    g_auto_session = false;
    g_cloud_ready = false;
    g_wake_upload_pending = false;
    ESP_LOGW(TAG, "wake_audio_candidate_aborted wake_id=%s", g_wake_id);
    display_ui_state(UiState::Connecting, "音频上传未完整，等待本次会话结束");
    if (g_ws && esp_websocket_client_is_connected(g_ws) && !g_wake_upload_release_pending.load()) {
        cJSON *end = cJSON_CreateObject();
        cJSON_AddStringToObject(end, "type", "session_end");
        cJSON_AddStringToObject(end, "wake_id", g_wake_id);
        if (g_manual_active.load()) cJSON_AddStringToObject(end, "manual_id", g_manual_id);
        if (!send_json(end)) ESP_LOGW(TAG, "wake_audio_session_end_unconfirmed wake_id=%s", g_wake_id);
    }
}

static bool upload_pending_wake_audio() {
    if (g_wake_upload_blocked.load()) return false;
    if (!g_wake_upload_pending.exchange(false)) return true;
    if (g_wake_tx_enabled.load()) { g_wake_tx_ready = true; return true; }
    // 同步断线/idle回调可能发生在send返回前；确认留给下一采音循环消费。
    g_wake_upload_blocked = true;
    if (!g_wake_upload_release_pending.load() && upload_wake_audio() && !g_wake_upload_release_pending.load() && g_ws && esp_websocket_client_is_connected(g_ws) && g_listening.load() && g_cloud_ready.load()) {
        g_wake_upload_blocked = false;
        return true;
    }
    abort_wake_audio();
    return false;
}

static void note_wake_clip_audio(size_t bytes, bool voice) {
    if (!g_wake_clip_active) return;
    const uint32_t samples = bytes / sizeof(int16_t);
    g_wake_clip_samples += samples;
    g_wake_clip_quiet_samples = voice ? 0 : g_wake_clip_quiet_samples + samples;
}

static bool finish_wake_clip() {
    if (!g_wake_clip_active) return true;
    const int64_t detected_us = g_wake_detected_us.load();
    const bool limited = g_wake_clip_samples >= WAKE_CLIP_LIMIT_SAMPLES || (detected_us && esp_timer_get_time() - detected_us >= 8000000);
    if (!limited && g_wake_clip_quiet_samples < WAKE_CLIP_QUIET_SAMPLES) return true;
    if (g_wake_tx_enabled.load()) {
        WakeTxChunk end = {};
        end.generation = g_wake_tx_generation.load();
        end.audio_bytes = g_wake_tx_queued_bytes;
        end.ended_ms = detected_us ? (esp_timer_get_time() - detected_us) / 1000 : 0;
        end.limited = limited;
        if (xQueueSend(g_wake_tx_queue, &end, 0) != pdTRUE) { abort_wake_audio(); return false; }
        g_wake_clip_active = false;
        return true;
    }
    cJSON *boundary = cJSON_CreateObject();
    cJSON_AddStringToObject(boundary, "type", "wake_clip_end");
    cJSON_AddStringToObject(boundary, "wake_id", g_wake_id);
    cJSON_AddNumberToObject(boundary, "audio_bytes", g_wake_clip_sent_bytes);
    cJSON_AddStringToObject(boundary, "reason", limited ? "limit" : "silence");
    const int64_t ready_us = g_wake_cloud_ready_us.load();
    if (detected_us && ready_us >= detected_us && g_wake_first_upload_us >= ready_us) {
        cJSON_AddNumberToObject(boundary, "device_detect_to_ready_ms", (ready_us - detected_us) / 1000);
        cJSON_AddNumberToObject(boundary, "device_ready_to_first_upload_ms", (g_wake_first_upload_us - ready_us) / 1000);
        cJSON_AddNumberToObject(boundary, "device_detect_to_end_ms", (esp_timer_get_time() - detected_us) / 1000);
    }
    if (!send_json(boundary)) { abort_wake_audio(); return false; }
    ESP_LOGI(TAG, "wake_clip_end wake_id=%s audio_bytes=%lu", g_wake_id, static_cast<unsigned long>(g_wake_clip_sent_bytes));
    g_wake_clip_active = false;
    return true;
}

static bool queue_wake_audio(const uint8_t *data, size_t length) {
    if (!g_wake_tx_enabled.load() || !g_wake_tx_queue) return false;
    for (size_t offset = 0; offset < length; offset += 640) {
        // 预留一个结束标记槽；容量不足不覆盖旧帧，也不悄悄退回同步发送。
        if (uxQueueSpacesAvailable(g_wake_tx_queue) <= 1) { ESP_LOGE(TAG, "wake_tx_queue_full wake_id=%s queued_bytes=%lu", g_wake_id, static_cast<unsigned long>(g_wake_tx_queued_bytes)); return false; }
        WakeTxChunk chunk = {};
        chunk.generation = g_wake_tx_generation.load();
        chunk.length = length - offset < sizeof(chunk.data) ? length - offset : sizeof(chunk.data);
        memcpy(chunk.data, data + offset, chunk.length);
        if (xQueueSend(g_wake_tx_queue, &chunk, 0) != pdTRUE) return false;
        g_wake_tx_queued_bytes += chunk.length;
    }
    return g_wake_tx_copying_prefix || finish_wake_clip();
}

static bool service_wake_audio_tx() {
    if (!g_wake_tx_enabled.load() || !g_ws || !esp_websocket_client_is_connected(g_ws)) return false;
    EXT_RAM_BSS_ATTR static WakeTxChunk chunk;
    chunk = {};
    if (xQueuePeek(g_wake_tx_queue, &chunk, 0) != pdTRUE || (!g_wake_tx_ready.load() && !chunk.manual_start)) return false;
    if (xQueueReceive(g_wake_tx_queue, &chunk, 0) != pdTRUE) return false;
    g_wake_tx_inflight = true;
    if (!g_wake_tx_enabled.load() || chunk.generation != g_wake_tx_generation.load()) { g_wake_tx_inflight = false; return true; }
    bool complete = false;
    if (chunk.manual_start) {
        cJSON *start = cJSON_CreateObject();
        cJSON_AddStringToObject(start, "type", "listen");
        cJSON_AddStringToObject(start, "state", "start");
        cJSON_AddStringToObject(start, "mode", "manual");
        cJSON_AddStringToObject(start, "manual_id", g_manual_id);
        // 允许同步ready回调，但失败必须由采音owner隔离，不能继续发PCM。
        g_manual_start_sent = true;
        complete = send_json(start);
    } else if (chunk.manual_end && chunk.audio_bytes == g_wake_tx_sent_bytes) {
        cJSON *end = cJSON_CreateObject();
        cJSON_AddStringToObject(end, "type", "listen");
        cJSON_AddStringToObject(end, "state", "stop");
        cJSON_AddStringToObject(end, "mode", "manual");
        cJSON_AddStringToObject(end, "manual_id", g_manual_id);
        cJSON_AddNumberToObject(end, "audio_bytes", g_wake_tx_sent_bytes);
        complete = send_json(end);
        if (complete && chunk.generation == g_wake_tx_generation.load()) {
            g_manual_end_sent = true;
            ESP_LOGI(TAG, "manual_upload_end manual_id=%s captured_bytes=%lu queued_bytes=%lu sent_bytes=%lu", g_manual_id, static_cast<unsigned long>(g_manual_captured_bytes), static_cast<unsigned long>(chunk.audio_bytes), static_cast<unsigned long>(g_wake_tx_sent_bytes));
        }
    } else if (chunk.length) {
        EXT_RAM_BSS_ATTR static uint8_t batch[WAKE_TX_BATCH_BYTES];
        size_t length = chunk.length;
        memcpy(batch, chunk.data, length);
        EXT_RAM_BSS_ATTR static WakeTxChunk next;
        next = {};
        // 只合并当前已存在的同代数据；不等待满包，不跨首句结束标记。
        while (xQueuePeek(g_wake_tx_queue, &next, 0) == pdTRUE && next.length && next.generation == chunk.generation && length + next.length <= sizeof(batch)) {
            if (xQueueReceive(g_wake_tx_queue, &next, 0) != pdTRUE) break;
            memcpy(batch + length, next.data, next.length);
            length += next.length;
        }
        const int64_t begin_us = esp_timer_get_time();
        if (!g_wake_first_upload_us.load()) g_wake_first_upload_us = begin_us;
        complete = esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(batch), length, pdMS_TO_TICKS(1000)) == static_cast<int>(length);
        const int64_t elapsed_us = esp_timer_get_time() - begin_us;
        // 取消期间只等旧send退出，不把旧统计或字节数写入新代。
        if (chunk.generation == g_wake_tx_generation.load()) {
            g_wake_tx_total_us += elapsed_us;
            if (elapsed_us > g_wake_tx_max_us.load()) g_wake_tx_max_us = elapsed_us;
            if (complete) {
                if (!g_wake_first_upload_done_us.load()) g_wake_first_upload_done_us = esp_timer_get_time();
                g_wake_tx_sent_bytes += length;
            }
        }
    } else if (chunk.audio_bytes == g_wake_tx_sent_bytes) {
        cJSON *boundary = cJSON_CreateObject();
        cJSON_AddStringToObject(boundary, "type", "wake_clip_end");
        cJSON_AddStringToObject(boundary, "wake_id", g_wake_id);
        cJSON_AddNumberToObject(boundary, "audio_bytes", chunk.audio_bytes);
        cJSON_AddStringToObject(boundary, "reason", chunk.limited ? "limit" : "silence");
        const int64_t detected_us = g_wake_detected_us.load();
        const int64_t ready_us = g_wake_cloud_ready_us.load();
        const int64_t first_us = g_wake_first_upload_us.load();
        if (detected_us) {
            if (ready_us >= detected_us) cJSON_AddNumberToObject(boundary, "device_detect_to_ready_ms", (ready_us - detected_us) / 1000);
            if (first_us >= ready_us && ready_us >= detected_us) cJSON_AddNumberToObject(boundary, "device_ready_to_first_upload_ms", (first_us - ready_us) / 1000);
            if (first_us >= detected_us) cJSON_AddNumberToObject(boundary, "device_detect_to_first_upload_ms", (first_us - detected_us) / 1000);
            if (first_us && ready_us >= detected_us) cJSON_AddNumberToObject(boundary, "device_upload_before_cloud_ready", first_us < ready_us ? 1 : 0);
            const int64_t input_ready_us = g_wake_input_ready_us.load();
            if (input_ready_us >= detected_us) cJSON_AddNumberToObject(boundary, "device_detect_to_input_ready_ms", (input_ready_us - detected_us) / 1000);
            if (input_ready_us >= detected_us && first_us >= input_ready_us) cJSON_AddNumberToObject(boundary, "device_input_ready_to_first_upload_ms", (first_us - input_ready_us) / 1000);
            cJSON_AddNumberToObject(boundary, "device_detect_to_end_ms", chunk.ended_ms);
            cJSON_AddNumberToObject(boundary, "device_end_to_marker_send_ms", (esp_timer_get_time() - detected_us) / 1000 - chunk.ended_ms);
            cJSON_AddNumberToObject(boundary, "device_first_upload_send_ms", (g_wake_first_upload_done_us.load() - first_us) / 1000);
            cJSON_AddNumberToObject(boundary, "device_uplink_send_ms", g_wake_tx_total_us.load() / 1000);
            cJSON_AddNumberToObject(boundary, "device_uplink_max_send_ms", g_wake_tx_max_us.load() / 1000);
        }
        complete = send_json(boundary);
    }
    if (!complete && chunk.generation == g_wake_tx_generation.load()) { g_wake_tx_ready = false; g_wake_tx_failed = true; }
    g_wake_tx_inflight = false;
    return true;
}

static void wake_audio_tx_task(void *) {
    while (true) { if (!service_wake_audio_tx()) vTaskDelay(pdMS_TO_TICKS(5)); else taskYIELD(); }
}

static void service_manual_listening() {
    const uint32_t request = g_manual_request.load();
    const bool held = g_manual_held.load();
    const int64_t now = esp_timer_get_time();
    if (g_manual_cancel_requested.exchange(false) || (g_manual_active.load() && request != g_manual_owner_request)) {
        if (g_manual_active.load()) {
            ++g_generation;
            note_playback_stopped();
            g_abort_requested = true;
            if (g_audio_queue) xQueueReset(g_audio_queue);
            abort_wake_audio();
        }
        g_manual_media_waiting = false;
        return;
    }
    if (g_manual_active.load()) {
        if (g_wake_upload_blocked.load()) return;
        if (board_profile::manual_audio_only && audio_capture_failed()) {
            ESP_LOGE(TAG, "manual_capture_incomplete manual_id=%s", g_manual_id);
            abort_wake_audio();
            return;
        }
        if (g_manual_ready_ack_token.load() == g_manual_owner_request.load() && g_manual_start_sent.load() && g_wake_tx_enabled.load() && !g_wake_upload_release_pending.load()) g_wake_tx_ready = true;
        // 建连、最长采集和松键后的积压都有墙钟上限；不补静音、不覆盖旧PCM。
        if ((!g_wake_tx_ready.load() && now - g_manual_started_us >= 4000000) ||
            (g_manual_releasing && !g_manual_end_sent.load() && now - g_manual_ended_us >= 8000000)) {
            ESP_LOGW(TAG, "manual_upload_timeout manual_id=%s", g_manual_id);
            abort_wake_audio();
            return;
        }
        if (!g_manual_releasing && now - g_manual_started_us >= 30000000) {
            ESP_LOGW(TAG, "manual_capture_limit manual_id=%s", g_manual_id);
            abort_wake_audio();
            display_ui_state(UiState::Idle, "录音超过30秒，本轮未执行，请重新录音");
            return;
        }
        if (!g_manual_ended && !held) {
            if (!g_manual_releasing) {
                g_manual_releasing = true;
                g_manual_ended_us = now;
                if (!audio_capture_pending(&g_manual_pending) || (g_manual_capture_seen && (g_manual_pending.generation != g_manual_capture_generation || g_manual_pending.sample_index != g_manual_capture_expected_sample))) { abort_wake_audio(); return; }
            }
            // 只排空松键时最终AFE输出ring中已有的有限尾帧，不等待新PCM。
            if (g_manual_pending.bytes) return;
            WakeTxChunk end = {};
            end.generation = g_wake_tx_generation.load();
            end.manual_end = true;
            end.audio_bytes = g_wake_tx_queued_bytes;
            g_manual_ended = true;
            g_listening = false;
            g_requested_ui = UiState::Thinking;
            display_ui_state(UiState::Thinking);
            if (xQueueSend(g_wake_tx_queue, &end, 0) != pdTRUE) abort_wake_audio();
            else ESP_LOGI(TAG, "manual_capture_end manual_id=%s queued_bytes=%lu", g_manual_id, static_cast<unsigned long>(end.audio_bytes));
        }
        return;
    }
    if (!held) { g_manual_media_waiting = false; return; }
    if (!request || request == g_manual_owner_request || g_wake_upload_blocked.load() || g_wake_upload_release_pending.load() || g_wake_tx_inflight.load()) return;
    if (!g_ws || !esp_websocket_client_is_connected(g_ws) || !g_manual_input_ready_supported.load()) {
        g_manual_owner_request = request;
        display_ui_state(UiState::Connecting, "手动录音需连接支持的网关");
        return;
    }
    // 已松开的旧请求只等待媒体真正停完；旧回执不能满足新请求的等待条件。
    if (g_manual_media_waiting && request != g_manual_media_request.load()) {
        if (g_media_active.load() || g_media_wake_pending.load()) {
            if (now - g_manual_started_us >= 4000000) { g_manual_owner_request = request; g_manual_media_waiting = false; }
            return;
        }
        g_manual_media_waiting = false;
    }
    if (g_media_active.load()) {
        if (!g_manual_media_waiting) {
            // 命令可能立即被播放任务消费并同步回执，所有等待状态须先于入队发布。
            g_manual_media_ready = 0;
            g_manual_media_request = request;
            g_manual_media_waiting = true;
            g_media_wake_pending = true;
            g_manual_started_us = now;
            MediaCommand command = {}; command.kind = MediaCommandKind::Manual; command.manual_request = request;
            if (!queue_media_command(command)) { g_manual_owner_request = request; g_manual_media_waiting = false; g_media_wake_pending = false; ESP_LOGW(TAG, "manual_media_command_queue_full"); return; }
        }
        if (now - g_manual_started_us >= 4000000) { g_manual_owner_request = request; g_manual_media_waiting = false; ESP_LOGW(TAG, "manual_media_pause_timeout"); }
        return;
    }
    if (g_media_wake_pending.load()) {
        if (g_manual_media_waiting && now - g_manual_started_us >= 4000000) { g_manual_owner_request = request; g_manual_media_waiting = false; ESP_LOGW(TAG, "manual_media_receipt_timeout"); }
        return;
    }
    if (g_manual_media_waiting && g_manual_media_ready.load() != request) {
        if (now - g_manual_started_us >= 4000000) { g_manual_owner_request = request; g_manual_media_waiting = false; }
        return;
    }
    if (g_wake_tx_enabled.load() || g_cloud_ready.load() || g_auto_session.load() || g_speaking.load()) {
        if (g_speaking.load()) control_protocol_abort();
        abort_wake_audio();
        return;
    }
    if (!g_wake_tx_queue) { g_manual_owner_request = request; ESP_LOGE(TAG, "manual_tx_queue_missing"); return; }
    reset_wake_audio_cache();
    audio_capture_discard_pending();
    g_navigation_id[0] = '\0';
    display_ui_nav_icon(0);
    g_manual_owner_request = request;
    g_manual_media_waiting = false;
    g_manual_started_us = now;
    snprintf(g_manual_id, sizeof(g_manual_id), "manual-%lu", static_cast<unsigned long>(request));
    g_manual_active = true;
    g_auto_session = false;
    g_wake_upload_pending = false;
    g_listening = true;
    g_wake_tx_enabled = true;
    WakeTxChunk start = {};
    start.generation = g_wake_tx_generation.load();
    start.manual_start = true;
    if (xQueueSend(g_wake_tx_queue, &start, 0) != pdTRUE) { abort_wake_audio(); return; }
    set_active_wifi_performance(true);
    display_ui_state(UiState::Connecting);
    ESP_LOGI(TAG, "manual_capture_start manual_id=%s", g_manual_id);
}

static bool send_capture_audio(const uint8_t *frame, size_t bytes) {
    if (g_wake_tx_enabled.load()) {
        if (!queue_wake_audio(frame, bytes)) { abort_wake_audio(); return false; }
        return true;
    }
    const int sent = esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(frame), bytes, pdMS_TO_TICKS(1000));
    if (!g_wake_clip_active) return true;
    if (sent != static_cast<int>(bytes)) { abort_wake_audio(); return false; }
    g_wake_clip_sent_bytes += bytes;
    return finish_wake_clip();
}

static bool capture_manual_audio(const uint8_t *frame, size_t bytes, const AudioCaptureFrame *metadata = nullptr) {
    if (!g_manual_active.load()) return false;
    if (!g_manual_ended && !g_media_active && !g_chime_playing) {
        if (metadata) {
            if (g_manual_capture_seen && (!metadata->continuous || metadata->generation != g_manual_capture_generation || metadata->sample_index != g_manual_capture_expected_sample)) { abort_wake_audio(); return true; }
            g_manual_capture_seen = true;
            g_manual_capture_generation = metadata->generation;
            g_manual_capture_expected_sample = metadata->sample_index + bytes / sizeof(int16_t);
        }
        if (g_manual_releasing) {
            if (!metadata || metadata->generation != g_manual_pending.generation || metadata->sample_index != g_manual_pending.sample_index) { abort_wake_audio(); return true; }
            if (bytes > g_manual_pending.bytes) bytes = g_manual_pending.bytes;
            g_manual_pending.bytes -= bytes;
            g_manual_pending.sample_index += bytes / sizeof(int16_t);
        }
        g_manual_captured_bytes += bytes;
        if (!queue_wake_audio(frame, bytes)) abort_wake_audio();
    }
    return true;
}

// 媒体命令只在播放任务执行；write 成功计数和暂停回执因此不会交叉。
static void finish_media(const char *state, const char *reason = nullptr, const char *error = nullptr) {
    g_drop_old_audio = true;
    ++g_generation;
    if (g_audio_queue) xQueueReset(g_audio_queue);
    if (strcmp(state, "ended") != 0) audio_playback_abort();
    note_playback_stopped();
    g_media_echo_until_us = esp_timer_get_time() + kUplinkEchoTailUs;
    g_media_active = false;
    g_media_end_received = false;
    g_response_finished = false;
    g_requested_ui = UiState::Idle;
    display_ui_audio_level(0);
    send_media_ack(state, reason, error);
    if (!reason || strcmp(reason, "wake_word") != 0) g_wake_reset_pending = true;
    display_ui_state(error ? UiState::Error : UiState::Idle, error ? "媒体播放未完成，请查看后台错误" : strcmp(state, "pause") == 0 ? "媒体已暂停，明确说继续后恢复" : "媒体播放已结束");
}

static void service_media_commands() {
    if (g_media_error_pending.exchange(false) && g_media_active) finish_media("error", nullptr, "command_queue_full");
    MediaCommand command;
    while (xQueueReceive(g_media_commands, &command, 0) == pdTRUE) {
        if (command.kind == MediaCommandKind::Start) {
            if (g_media_active && strcmp(command.playback_id, g_media_id) == 0) { send_media_ack("start"); continue; }
            if (g_media_active || g_media_wake_pending || g_speaking || g_listening || g_auto_session || g_cloud_ready || g_chime_playing || ota_update_busy()) { send_media_ack("error", nullptr, "audio_busy", command.playback_id); continue; }
            const esp_err_t prepared = audio_prepare_pcm24();
            if (prepared != ESP_OK) { send_media_ack("error", nullptr, "audio_not_ready", command.playback_id); continue; }
            ++g_generation;
            xQueueReset(g_audio_queue);
            snprintf(g_media_id, sizeof(g_media_id), "%s", command.playback_id);
            snprintf(g_media_title, sizeof(g_media_title), "%s", command.title);
            g_media_played_samples = command.position_samples;
            g_media_underruns = 0;
            g_queue_overflows = 0;
            g_media_end_received = false;
            g_response_finished = false;
            g_auto_session = false;
            g_cloud_ready = false;
            g_media_wake_pending = false;
            g_wake_reset_pending = true;
            g_playback_started_us = esp_timer_get_time();
            g_speaking = true;
            g_requested_ui = UiState::Speaking;
            g_drop_old_audio = false;
            g_media_active = true;
            g_media_progress_us = esp_timer_get_time();
            display_ui_state(UiState::Speaking, g_media_title, true);
            send_media_ack("start");
            ESP_LOGI(TAG, "media_started playback_id=%s position_samples=%lu", g_media_id, static_cast<unsigned long>(g_media_played_samples.load()));
            continue;
        }
        if (command.kind == MediaCommandKind::Wake || command.kind == MediaCommandKind::Manual) {
            if (!g_media_active) { g_media_wake_pending = false; continue; }
            const bool manual = command.kind == MediaCommandKind::Manual;
            if (manual) g_media_manual_request = command.manual_request;
            g_media_wake_pending = true;
            finish_media("pause", manual ? "button" : "wake_word");
            continue;
        }
        if (command.kind == MediaCommandKind::Error) { if (g_media_active && strcmp(command.playback_id, g_media_id) == 0) finish_media("error", nullptr, command.error); continue; }
        if (!g_media_id[0] || strcmp(command.playback_id, g_media_id) != 0) { send_media_ack("error", nullptr, "stale_playback_id", command.playback_id); continue; }
        if (command.kind == MediaCommandKind::End) {
            if (g_media_active) { g_media_end_received = true; g_response_finished = true; g_drop_old_audio = true; }
            else send_media_ack("error", nullptr, "media_not_active");
        } else if (g_media_active) finish_media(command.kind == MediaCommandKind::Pause ? "pause" : "stop");
        else send_media_ack(command.kind == MediaCommandKind::Pause ? "pause" : "stop");
    }
    const int64_t now = esp_timer_get_time();
    if (g_media_active && now - g_media_progress_us >= 1000000) { g_media_progress_us = now; send_media_ack("progress"); }
}

static void capture_task(void *) {
    uint8_t frame[640];
    int64_t media_near_voice_us = 0;
    if (audio_capture_start() != ESP_OK) { ESP_LOGE(TAG, "连续采音启动失败"); vTaskDelete(nullptr); return; }
    while (true) {
        if (g_wake_tx_failed.exchange(false)) abort_wake_audio();
        if (g_wake_tx_enabled.load() && g_wake_clip_active) (void)finish_wake_clip();
        if (g_audio_diagnostic_requested.exchange(false)) {
            const esp_err_t diagnostic_result = audio_diagnostic_start(160);
            g_audio_diagnostic_export = diagnostic_result == ESP_OK;
            g_audio_diagnostic_chunks = 0;
            ESP_LOGI(TAG, "audio_diagnostic_requested result=%d", diagnostic_result);
        }
        if (g_audio_diagnostic_export && audio_diagnostic_ready()) {
            static uint8_t diagnostic_pcm[3072];
            static unsigned char diagnostic_base64[4097];
            const size_t diagnostic_bytes = audio_diagnostic_read(diagnostic_pcm, sizeof(diagnostic_pcm));
            size_t encoded_bytes = 0;
            cJSON *diagnostic = cJSON_CreateObject();
            cJSON_AddStringToObject(diagnostic, "type", "audio_diagnostic");
            if (diagnostic_bytes && mbedtls_base64_encode(diagnostic_base64, sizeof(diagnostic_base64), &encoded_bytes, diagnostic_pcm, diagnostic_bytes) == 0) {
                diagnostic_base64[encoded_bytes] = '\0';
                cJSON_AddStringToObject(diagnostic, "pcm", reinterpret_cast<char *>(diagnostic_base64));
                if (++g_audio_diagnostic_chunks == 160) { g_audio_diagnostic_export = false; cJSON_AddBoolToObject(diagnostic, "done", true); }
            } else { g_audio_diagnostic_export = false; cJSON_AddBoolToObject(diagnostic, "done", true); }
            send_json(diagnostic);
        }
        if (g_abort_requested.exchange(false)) { reset_barge_state(false); audio_playback_abort(); display_ui_audio_level(0); }
        if (service_wake_audio_reset()) media_near_voice_us = 0;
        service_manual_listening();
        service_active_wifi_performance();
        {
            static int64_t last_ping_us;
            const int64_t now_us = esp_timer_get_time();
            if (now_us - last_ping_us > kWsPingIntervalUs) {
                last_ping_us = now_us;
                send_control("device_ping");
            }
            const int64_t last_downlink = g_last_downlink_us.load();
            if (last_downlink && now_us - last_downlink > kWsDownlinkRestartUs) {
                ESP_LOGE(TAG, "ws_downlink_timeout restart");
                esp_restart();
            }
        }
        const bool speaking_now = g_speaking.load();
        if (speaking_now != g_interrupt_threshold_active) {
            g_interrupt_threshold_active = speaking_now;
            wake_word_set_threshold(speaking_now ? kWakeThresholdInterrupt : kWakeThresholdStandby);
        }
        AudioCaptureFrame capture_frame = {};
        const size_t bytes = audio_capture_read(frame, sizeof(frame), &capture_frame);
        if (!bytes) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        if (g_wake_upload_blocked.load()) { vTaskDelay(1); continue; }
        // 松键后sender仍独立排空已采PCM；此处不再采新帧，也不落入自动唤醒缓存。
        if (capture_manual_audio(frame, bytes, &capture_frame)) { vTaskDelay(1); continue; }
        // Explicit manual-only profiles never cache idle microphone PCM,
        // enter automatic listening, or upload playback echo without AEC.
        if constexpr (board_profile::manual_audio_only) { vTaskDelay(1); continue; }
        note_wake_clip_audio(bytes, !capture_frame.vad_valid || capture_frame.voice_active);
        AudioCaptureMetrics metrics = {};
        const bool have_metrics = audio_capture_metrics(&metrics);
        if (!g_listening && !g_media_active && !g_media_wake_pending && esp_timer_get_time() < g_media_echo_until_us.load()) { vTaskDelay(1); continue; }
        if (g_media_active || g_media_wake_pending) {
            if (!g_media_wake_pending) {
                // [人工决策-2026-11-04 22:07:49] 牛牛要求完整唤醒词可暂停媒体；普通回答的语音打断继续关闭。
                // 独立媒体唤醒入口不启用普通 TTS barge-in。保留 AEC 后完整唤醒缓存，
                // 本地命中只暂停，最终真人唤醒仍由现有网关 ASR 校验。
                cache_wake_audio(frame, bytes);
                note_wake_boundary_audio(bytes, capture_frame);
                const int64_t now = esp_timer_get_time();
                if (have_metrics && near_voice(metrics)) media_near_voice_us = now;
                if (!have_metrics || (metrics.reference_rms >= 20 && fabsf(metrics.echo_correlation) >= 0.60f)) memset(frame, 0, bytes);
                if (g_wake_word_ready && audio_echo_cancellation_ready() && wake_word_feed(reinterpret_cast<const int16_t *>(frame), bytes / sizeof(int16_t)) && media_near_voice_us && now - media_near_voice_us <= 800000) {
                    snapshot_wake_prefix();
                    g_media_wake_pending = true;
                    MediaCommand command = {}; command.kind = MediaCommandKind::Wake;
                    if (!queue_media_command(command)) { g_media_wake_pending = false; g_wake_reset_pending = true; ESP_LOGW(TAG, "media_wake_command_queue_full"); }
                }
            } else cache_wake_audio(frame, bytes);
            vTaskDelay(1); continue;
        }
        // 提示音及短尾音不喂给唤醒；若用户已按键开始收听，播放任务会迅速让路。
        if (g_chime_playing || (!g_listening && esp_timer_get_time() < g_chime_echo_until_us.load())) {
            if (g_listening && g_cloud_ready && g_ws && esp_websocket_client_is_connected(g_ws)) { memset(frame, 0, bytes); esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(frame), bytes, pdMS_TO_TICKS(1000)); }
            vTaskDelay(1); continue;
        }
        if (g_barge_confirmed_ack.exchange(false) && g_barge_state.load() == BargeState::Confirmed) {
            upload_barge_cache();
            g_barge_state = BargeState::None;
            reset_barge_cache();
            ESP_LOGI(TAG, "barge_state confirmed_ack upload_preroll=1");
        }
        if (g_barge_resume_ack.exchange(false) && g_barge_state.load() == BargeState::Candidate) reset_barge_state(false);
        const uint32_t diagnostic_now = esp_timer_get_time() / 1000;
        if (g_diagnostics && diagnostic_now - g_last_diagnostic >= 1000) {
            g_last_diagnostic = diagnostic_now;
            cJSON *report = cJSON_CreateObject();
            cJSON_AddStringToObject(report, "type", "barge_diagnostic");
            cJSON_AddNumberToObject(report, "barge_state", static_cast<unsigned>(g_barge_state.load()));
            cJSON_AddBoolToObject(report, "speaking", g_speaking.load());
            cJSON_AddNumberToObject(report, "queue_depth", uxQueueMessagesWaiting(g_audio_queue));
            cJSON_AddNumberToObject(report, "played_samples", g_barge_played_samples.load());
            cJSON_AddNumberToObject(report, "overflows", g_queue_overflows.load());
            cJSON_AddBoolToObject(report, "voice", metrics.voice_active);
            cJSON_AddNumberToObject(report, "clean_rms", metrics.clean_rms);
            cJSON_AddNumberToObject(report, "ref_rms", metrics.reference_rms);
            cJSON_AddNumberToObject(report, "correlation", metrics.echo_correlation);
            cJSON_AddNumberToObject(report, "baseline", g_clean_rms_baseline);
            cJSON_AddNumberToObject(report, "echo_floor", g_echo_floor_rms);
            cJSON_AddNumberToObject(report, "candidate_peak", g_candidate_peak_rms);
            send_json(report);
        }
        const bool voice = have_metrics && near_voice(metrics);
        const BargeState barge_state = g_barge_state.load();
        if (barge_state != BargeState::None) cache_barge_audio(frame, bytes);
        if (g_listening && g_ws && esp_websocket_client_is_connected(g_ws)) {
            if (!g_cloud_ready) { cache_wake_audio(frame, bytes); continue; }
            if (!upload_pending_wake_audio()) continue;
            bool send_capture = true;
            // 0.3.5：g_speaking 置 false 之后 codec/I2S 仍会输出约 100-300ms 尾音，
            // 这里按播放结束时间戳判定是否还处于"尾音静音窗口"内。
            const int64_t playback_end_us = g_playback_end_us.load();
            const bool uplink_echo_tail = !g_speaking && playback_end_us > 0 &&
                esp_timer_get_time() - playback_end_us < kUplinkEchoTailUs;
            if (g_speaking && barge_state == BargeState::None) {
                if (g_natural_barge_in.load() && audio_echo_cancellation_ready() && !g_test_disable_barge) {
                    // 持续上传已处理音频，包括插话开头；判断有效插话交给千问语义VAD。
                    // 不用本地响度硬切或唤醒词决定中止，设备仅执行服务端的interrupt。
                    g_loud_hold_us = 0;
                } else if (!kEnableBargeIn) {
                    // 0.3.6：语音打断关闭。播放期间只把上行帧清零后照常发送（静音帧，
                    // 保持云端连续流、也盖住 AEC 残留），不缓存打断音频、不跑本地强打断、
                    // 不喂唤醒词、不触发 candidate/confirmed/abort；回答完整播完后由
                    // 0.3.5 的 note_playback_stopped + 400ms 尾音窗口自然回到 listening。
                    memset(frame, 0, bytes);
                    g_loud_hold_us = 0;
                } else {
                cache_barge_audio(frame, bytes);
                // 自然打断=本地判定：回声残留本质是语音的复制品，上传给云端 VAD
                // 必然触发"回答-打断"循环（0.3.3 实测），因此播放期上传保持静音，
                // 打断由本地"持续400ms超3000"判定（残留峰值帧稀疏，凑不满400ms）。
                // 打断确认后进 listening，上传自动恢复，云端正常收新内容。
                // MN 推理播放期 26-76ms 超帧周期，唤醒词仅兜底。
                // 按小智 speaking 状态仍启用唤醒检测；playback_id 只标识播放，不应禁止识别。
                // https://github.com/78/xiaozhi-esp32/blob/v2.5.2/main/application.cc
                if (have_metrics && metrics.voice_active && metrics.clean_rms >= kLoudInterruptRms) {
                    g_loud_hold_us += static_cast<uint32_t>((bytes * 1000000ULL) / (16000 * 2));
                    if (g_loud_hold_us >= kLoudInterruptHoldUs) {
                        g_loud_hold_us = 0;
                        ESP_LOGI(TAG, "loud_interrupt rms=%lu", static_cast<unsigned long>(metrics.clean_rms));
                        if (g_playback_id[0]) {
                            start_barge_candidate();
                            confirm_barge();
                        } else {
                            control_protocol_abort(); begin_listening(false); g_wake_upload_pending = true;
                        }
                        continue;
                    }
                } else {
                    g_loud_hold_us = 0;
                }
                memset(frame, 0, bytes);
                if (g_auto_session && wake_word_feed(reinterpret_cast<const int16_t *>(frame), bytes / sizeof(int16_t))) {
                    ESP_LOGI(TAG, "wake_phrase_interrupt reference=software_pcm24");
                    if (g_playback_id[0]) {
                        start_barge_candidate();
                        if (g_barge_state.load() == BargeState::Candidate) {
                            confirm_barge();
                        } else {
                            ESP_LOGW(TAG, "wake_barge_candidate_rejected fallback=abort");
                            control_protocol_abort();
                            begin_listening(false);
                            g_wake_upload_pending = true;
                        }
                    } else {
                        control_protocol_abort(); begin_listening(false); g_wake_upload_pending = true;
                    }
                    continue;
                }
                } // kEnableBargeIn == true：原有语音打断逻辑，恢复开关后自动生效
                vTaskDelay(1);
            } else if (g_barge_state.load() == BargeState::Candidate) {
                if (g_auto_session && wake_word_feed(reinterpret_cast<const int16_t *>(frame), bytes / sizeof(int16_t))) {
                    ESP_LOGI(TAG, "wake_phrase_interrupt during_candidate=1");
                    confirm_barge();
                }
                const uint32_t elapsed_us = g_barge_started_us ? static_cast<uint32_t>(esp_timer_get_time() - g_barge_started_us) : 0;
                const uint32_t frame_us = static_cast<uint32_t>((bytes * 1000000ULL) / (16000 * 2));
                if (elapsed_us >= BARGE_TAIL_US) {
                    if (metrics.clean_rms > g_candidate_peak_rms) g_candidate_peak_rms = metrics.clean_rms;
                    if (voice) {
                        g_barge_voice_us += frame_us;
                        g_barge_voice_gap_us = 0;
                    } else if (g_barge_voice_us > 0) {
                        g_barge_voice_gap_us += frame_us;
                        if (g_barge_voice_gap_us > 40000) { g_barge_voice_us = 0; g_barge_voice_gap_us = 0; }
                    }
                    if (g_barge_voice_us >= BARGE_VOICE_CONFIRM_US) confirm_barge();
                }
                if (g_barge_state.load() == BargeState::Candidate && elapsed_us >= BARGE_RESUME_US) reset_barge_state(true);
                memset(frame, 0, bytes);
                send_capture = false;
                vTaskDelay(1);
            } else if (g_barge_state.load() == BargeState::Confirmed) {
                if (g_barge_started_us && esp_timer_get_time() - g_barge_started_us > 1800000) {
                    ESP_LOGW(TAG, "barge_state confirmed_ack_timeout");
                    reset_barge_state(false); g_speaking = false; g_listening = false; g_cloud_ready = false;
                    send_control("session_end"); display_ui_state(UiState::Idle);
                }
                memset(frame, 0, bytes);
                send_capture = false;
                vTaskDelay(1);
            } else if (uplink_echo_tail && !g_natural_barge_in.load()) {
                // 播放刚结束的 400ms 内继续清零上传：这段是 codec/I2S 的尾音，
                // 立即恢复上行会被云端 qwen server_vad（0.75/600ms）判成人声，
                // 云端自动生成新回答、播完再触发，形成"自问自答"无限循环
                // （0.3.4 会话 09dd5247… 实测 9 轮回答且无任何真人打断）。
                // 注意必须照常发送静音帧、不能 continue 跳过：云端 VAD 需要连续上行流，
                // 断流会被当作会话结束；窗口内也不喂 wake_word_feed，尾音是播报内容的
                // 复制品，喂进去只会制造假唤醒。
                memset(frame, 0, bytes);
                g_loud_hold_us = 0;
            }
            if (send_capture && !send_capture_audio(frame, bytes)) continue;
        } else if (!g_listening && !g_speaking) {
            cache_wake_audio(frame, bytes);
            note_wake_boundary_audio(bytes, capture_frame);
            if (wake_word_feed(reinterpret_cast<const int16_t *>(frame), bytes / sizeof(int16_t))) {
                snapshot_wake_prefix();
                send_wake_detect();
                g_wake_upload_pending = true;
                begin_listening(false);
            }
            vTaskDelay(1);
        }
    }
}

static void playback_task(void *) {
    static AudioChunk chunk;
    bool have_chunk = false;
    uint32_t shown_generation = UINT32_MAX;
    uint32_t underruns = 0;
    uint32_t frames = 0;
    int64_t started = 0;
    int64_t first_write_after_tts_ms = -1;
    int64_t first_write_after_receive_ms = -1;
    int64_t first_display_ms = -1;
    uint32_t playback_generation = g_generation;
    while (true) {
        service_media_commands();
        if (g_media_active && g_media_end_received && !have_chunk && uxQueueMessagesWaiting(g_audio_queue) == 0) finish_media("ended");
        if (playback_generation != g_generation) {
            playback_generation = g_generation;
            shown_generation = UINT32_MAX; frames = 0; underruns = 0; started = 0; have_chunk = false;
            first_write_after_tts_ms = first_write_after_receive_ms = first_display_ms = -1;
        }
        if (g_barge_state.load() != BargeState::None) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        if (shown_generation == UINT32_MAX) {
            while (playback_generation == g_generation && g_barge_state.load() == BargeState::None && uxQueueMessagesWaiting(g_audio_queue) < 5 && !g_response_finished) { service_media_commands(); vTaskDelay(pdMS_TO_TICKS(5)); }
        }
        if (playback_generation != g_generation) continue;
        if (g_barge_state.load() != BargeState::None) continue;
        if (!have_chunk && xQueueReceive(g_audio_queue, &chunk, pdMS_TO_TICKS(40)) != pdTRUE) {
            if (shown_generation != UINT32_MAX && g_speaking) { ++underruns; if (g_media_active && !g_media_end_received) ++g_media_underruns; shown_generation = UINT32_MAX; }
            continue;
        }
        have_chunk = true;
        if (g_barge_state.load() != BargeState::None) continue;
        if (chunk.generation != g_generation) { have_chunk = false; continue; }
        if (chunk.length == 0) {
            have_chunk = false;
            // 播放自然结束：note_playback_stopped 会置 g_speaking=false 并记录结束时刻，
            // 采集循环据此再静音上传 400ms，盖住 codec/I2S 尾音（0.3.5 自问自答修复）。
            note_playback_stopped();
            g_wake_reset_pending = true; g_playback_id[0] = '\0'; display_ui_audio_level(0);
            UiState next_ui = g_requested_ui.load();
            if (next_ui == UiState::Speaking) next_ui = g_listening ? UiState::Listening : UiState::Idle;
            display_ui_state(next_ui);
            send_control("device_state", next_ui == UiState::Thinking ? "thinking" : next_ui == UiState::Executing ? "executing" : g_listening ? "listening" : "idle");
            shown_generation = UINT32_MAX;
            cJSON *stats = cJSON_CreateObject();
            cJSON_AddStringToObject(stats, "type", "audio_stats");
            cJSON_AddNumberToObject(stats, "frames", frames);
            cJSON_AddNumberToObject(stats, "underruns", underruns);
            cJSON_AddNumberToObject(stats, "overflows", g_queue_overflows);
            cJSON_AddNumberToObject(stats, "elapsed_ms", started ? (esp_timer_get_time() - started) / 1000 : 0);
            if (first_write_after_tts_ms >= 0) cJSON_AddNumberToObject(stats, "first_write_after_tts_ms", first_write_after_tts_ms);
            if (first_write_after_receive_ms >= 0) cJSON_AddNumberToObject(stats, "first_write_after_receive_ms", first_write_after_receive_ms);
            if (first_display_ms >= 0) cJSON_AddNumberToObject(stats, "first_display_ms", first_display_ms);
            send_json(stats);
            ESP_LOGI(TAG, "播放完成 frames=%lu underruns=%lu overflows=%lu stack_free=%lu", (unsigned long)frames, (unsigned long)underruns, (unsigned long)g_queue_overflows.load(), (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
            frames = 0; underruns = 0; started = 0; g_queue_overflows = 0; g_response_finished = false;
            first_write_after_tts_ms = first_write_after_receive_ms = first_display_ms = -1;
            continue;
        }
        const int64_t first_tts_us = frames == 0 ? g_tts_received_us.load() : 0;
        const int64_t first_received_us = frames == 0 ? g_first_pcm_received_us.load() : 0;
        if (shown_generation != chunk.generation) {
            const int64_t display_started_us = esp_timer_get_time();
            display_ui_state(UiState::Speaking, chunk.media ? g_media_title : nullptr, chunk.media);
            if (!frames) first_display_ms = (esp_timer_get_time() - display_started_us) / 1000;
            shown_generation = chunk.generation;
            if (!started) started = esp_timer_get_time();
        }
        ++frames;
        uint32_t energy = 0;
        const int16_t *samples = reinterpret_cast<const int16_t *>(chunk.data);
        const size_t count = chunk.length / sizeof(int16_t);
        for (size_t index = 0; index < count; ++index) energy += samples[index] < 0 ? -static_cast<int>(samples[index]) : samples[index];
        const uint32_t level = count ? (energy / count) * 100 / 6000 : 0;
        display_ui_audio_level(level > 100 ? 100 : level);
        const esp_err_t write_result = audio_play_pcm24(chunk.data, chunk.length);
        // write完成含I2S/codec耗时，不代表麦克风测到的扬声器起音。
        if (write_result == ESP_OK && frames == 1 && !chunk.media && chunk.generation == g_generation && first_tts_us == g_tts_received_us.load()) {
            const int64_t written_us = esp_timer_get_time();
            if (first_tts_us && written_us >= first_tts_us) first_write_after_tts_ms = (written_us - first_tts_us) / 1000;
            if (first_received_us && written_us >= first_received_us) first_write_after_receive_ms = (written_us - first_received_us) / 1000;
        }
        if (write_result == ESP_OK) {
            if (chunk.media) {
                if (g_media_played_samples.load() > UINT32_MAX - count) finish_media("error", nullptr, "position_limit");
                else g_media_played_samples.fetch_add(static_cast<uint32_t>(count));
            } else g_barge_played_samples.fetch_add(static_cast<uint32_t>(count));
        } else if (chunk.generation == g_generation) {
            if (chunk.media) finish_media("error", nullptr, "audio_write_failed");
            else display_ui_state(UiState::Error, "语音播放失败");
        }
        have_chunk = false;
    }
}

esp_err_t control_protocol_start(const DeviceConfig *config) {
    if (g_ws) return ESP_ERR_INVALID_STATE;
    g_media_receipts = xQueueCreateStatic(8, sizeof(MediaReceipt), g_media_receipts_storage, &g_media_receipts_control);
    if (!g_media_receipts) return ESP_ERR_NO_MEM;
    g_screen_action_queue = xQueueCreate(4, sizeof(ScreenAction));
    if (!g_screen_action_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(screen_action_task, "screen_actions", 8192, nullptr, 2, nullptr, board_profile::working_memory_caps) != pdPASS) return ESP_ERR_NO_MEM;
    if (!config || !config->ws_url[0]) return ESP_ERR_INVALID_ARG;
    g_wake_word_ready = wake_word_init() == ESP_OK;
#if defined(ASSISTANT_BOARD_GUITION) || defined(ASSISTANT_BOARD_INTERNAL_RAM)
    ESP_LOGI(TAG, "manual_audio_profile no local wake model; use hardware or screen input");
#else
    if (!g_wake_word_ready) ESP_LOGW(TAG, "伊迪斯离线口令不可用，仍可用 BOOT 按键");
#endif
#if defined(ASSISTANT_WAKE_PCM_PROBE)
    audio_capture_stop();
    if (board_profile::display_lock(1000)) { lv_timer_enable(false); bsp_display_unlock(); }
    const UBaseType_t previous_priority = uxTaskPriorityGet(nullptr);
    vTaskPrioritySet(nullptr, 5);
    bool negative_detected = false, positive_detected = false;
    const esp_err_t probe_result = wake_word_run_pcm_probe(&negative_detected, &positive_detected);
    vTaskPrioritySet(nullptr, previous_priority);
    if (board_profile::display_lock(1000)) { lv_timer_enable(true); bsp_display_unlock(); }
    audio_capture_start();
    ESP_LOGI(TAG, "wake_probe_result ok=%d negative=%d positive=%d", probe_result == ESP_OK, negative_detected, positive_detected);
#endif
    snprintf(g_headers, sizeof(g_headers), "Authorization: Bearer %s\r\nProtocol-Version: 1\r\n", config->device_token);
    snprintf(g_device_token, sizeof(g_device_token), "Bearer %s", config->device_token);
    // Each explicit mode bounds its queues and chooses its implemented memory pool.
    auto *queue_storage = static_cast<uint8_t *>(heap_caps_malloc(AUDIO_QUEUE_CAPACITY * sizeof(AudioChunk), board_profile::working_memory_caps));
    if (!queue_storage) return ESP_ERR_NO_MEM;
    g_audio_queue = xQueueCreateStatic(AUDIO_QUEUE_CAPACITY, sizeof(AudioChunk), queue_storage, &g_audio_queue_control);
    if (!g_audio_queue) { heap_caps_free(queue_storage); return ESP_ERR_NO_MEM; }
    g_media_commands = xQueueCreateStatic(4, sizeof(MediaCommand), g_media_commands_storage, &g_media_commands_control);
    if (!g_media_commands) return ESP_ERR_NO_MEM;
    if constexpr (!board_profile::manual_audio_only) {
        g_wake_cache = static_cast<uint8_t *>(heap_caps_malloc(WAKE_CACHE_BYTES, board_profile::working_memory_caps));
        if (!g_wake_cache) return ESP_ERR_NO_MEM;
        g_wake_prefix_cache = static_cast<uint8_t *>(heap_caps_malloc(WAKE_PREFIX_BYTES, board_profile::working_memory_caps));
        if (!g_wake_prefix_cache) return ESP_ERR_NO_MEM;
    }
    auto *wake_tx_storage = static_cast<uint8_t *>(heap_caps_malloc((WAKE_TX_AUDIO_CAPACITY + 1) * sizeof(WakeTxChunk), board_profile::working_memory_caps));
    if (!wake_tx_storage) return ESP_ERR_NO_MEM;
    g_wake_tx_queue = xQueueCreateStatic(WAKE_TX_AUDIO_CAPACITY + 1, sizeof(WakeTxChunk), wake_tx_storage, &g_wake_tx_queue_control);
    if (!g_wake_tx_queue) { heap_caps_free(wake_tx_storage); return ESP_ERR_NO_MEM; }
    if constexpr (!board_profile::manual_audio_only) {
        g_barge_cache = static_cast<uint8_t *>(heap_caps_malloc(BARGE_PREROLL_BYTES, board_profile::working_memory_caps));
        if (!g_barge_cache) return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(g_ws_client.Init(config->ws_url, g_headers), TAG, "WebSocket init");
    g_ws = g_ws_client.handle();
    esp_err_t result = esp_websocket_register_events(g_ws, WEBSOCKET_EVENT_ANY, ws_event, nullptr);
    if (result == ESP_OK) result = esp_websocket_client_start(g_ws);
    if (result != ESP_OK) {
        // No protocol sender tasks exist yet; reset outside the WS callback.
        (void)g_ws_client.Reset();
        g_ws = nullptr;
        ESP_LOGE(TAG, "WebSocket startup failed error=%s", esp_err_to_name(result));
        return result;
    }
    if (xTaskCreateWithCaps(wake_audio_tx_task, "wake_pcm_tx", 8192, nullptr, 5, nullptr, board_profile::working_memory_caps) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(capture_task, "capture_pcm", 8192, nullptr, 5, nullptr, board_profile::working_memory_caps) != pdPASS) return ESP_ERR_NO_MEM;
    // Preserve the measured8KB stack requirement; internal mode changes its pool, not its size.
    return xTaskCreateWithCaps(playback_task, "playback_pcm", 8192, nullptr, 6, nullptr, board_profile::working_memory_caps) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static void begin_listening(bool manual) {
    g_natural_barge_in = false;
    if (g_wake_upload_blocked.load()) return;
    g_navigation_id[0] = '\0';
    display_ui_nav_icon(0);
    if (!g_ws || !esp_websocket_client_is_connected(g_ws)) { display_ui_state(UiState::Connecting, "等待服务连接"); return; }
    if (g_speaking) control_protocol_abort();
    if (!g_listening) {
        g_auto_session = !manual;
        display_ui_state(g_cloud_ready ? UiState::Listening : UiState::Connecting);
        send_control("listen", "start", manual ? "manual" : audio_echo_cancellation_ready() ? "realtime" : "auto");
        g_listening = true;
        send_control("device_state", "listening");
    }
}
void control_protocol_listen_start() { if (!g_manual_held.exchange(true)) ++g_manual_request; }
void control_protocol_listen_stop() { g_manual_held = false; }
void control_protocol_listen_cancel() { g_manual_cancel_requested = true; g_manual_held = false; }
void control_protocol_abort() { g_natural_barge_in = false; if (g_manual_active.load()) { g_manual_held = false; g_manual_cancel_requested = true; return; } g_navigation_id[0] = '\0'; display_ui_nav_icon(0); if (g_media_active) { MediaCommand command = {}; command.kind = MediaCommandKind::Stop; snprintf(command.playback_id, sizeof(command.playback_id), "%s", g_media_id); queue_media_command(command); return; } reset_barge_state(false); ++g_generation; g_listening = false; note_playback_stopped(); g_response_finished = false; g_requested_ui = UiState::Idle; display_ui_audio_level(0); audio_playback_abort(); if (g_audio_queue) xQueueReset(g_audio_queue); send_control("abort"); send_control("device_state", "idle"); display_ui_state(UiState::Idle, board_profile::manual_audio_only ? "本次回答已停止，可按键重新录音" : "本次回答已停止，可重新唤醒继续"); }
void control_protocol_send_device_state(const char *state, const char *detail) { cJSON *root = cJSON_CreateObject(); cJSON_AddStringToObject(root, "type", "device_state"); cJSON_AddStringToObject(root, "state", state); if (detail) cJSON_AddStringToObject(root, "detail", detail); send_json(root); }
