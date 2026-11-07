// 插入control_protocol.cpp实际函数原文后，用合成字节与mock send验证行为。
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#define EXT_RAM_BSS_ATTR

static const char *TAG = "host-wake-upload";
static constexpr size_t WAKE_CACHE_BYTES = 16000 * 2 * 2;
static constexpr size_t WAKE_PREFIX_BYTES = 16000 * 2 * 4;
static uint8_t prefix_storage[WAKE_PREFIX_BYTES];
static uint8_t suffix_storage[WAKE_CACHE_BYTES];
static uint8_t *g_wake_cache = suffix_storage;
static size_t g_wake_cache_write;
static size_t g_wake_cache_count;
static uint8_t *g_wake_prefix_cache = prefix_storage;
static size_t g_wake_prefix_write;
static size_t g_wake_prefix_count;
static bool g_wake_prefix_frozen;
static std::atomic<bool> g_wake_clip_supported{false};
static bool g_wake_clip_active;
static bool g_wake_cache_overflow;
static uint32_t g_wake_clip_samples;
static uint32_t g_wake_clip_quiet_samples;
static uint32_t g_wake_clip_sent_bytes;
static constexpr uint32_t WAKE_CLIP_QUIET_SAMPLES = 16000 * 300 / 1000;
static constexpr uint32_t WAKE_CLIP_LIMIT_SAMPLES = 16000 * 8;
static std::atomic<bool> g_wake_upload_pending{false};
static std::atomic<bool> g_wake_upload_blocked{false};
static std::atomic<bool> g_wake_upload_release_pending{false};
static std::atomic<bool> g_wake_reset_pending{false};
static std::atomic<bool> g_listening{false};
static std::atomic<bool> g_cloud_ready{false};
static std::atomic<bool> g_auto_session{false};
static std::atomic<bool> g_speaking{false};
enum class UiState { Idle, Connecting, Listening, Thinking };
static std::atomic<UiState> g_requested_ui{UiState::Idle};
static uint32_t g_next_wake_id;
static char g_wake_id[32];
static char g_navigation_id[40];
static std::atomic<int64_t> g_wake_detected_us{0};
static std::atomic<int64_t> g_wake_cloud_ready_us{0};
static std::atomic<int64_t> g_wake_input_ready_us{0};
static std::atomic<int64_t> g_wake_first_upload_us{0};
static std::atomic<int64_t> g_wake_first_upload_done_us{0};
static std::atomic<int64_t> g_wake_tx_total_us{0};
static std::atomic<int64_t> g_wake_tx_max_us{0};
struct WakeTxChunk { uint32_t generation; uint32_t audio_bytes; uint32_t ended_ms; uint16_t length; bool limited; bool manual_start; bool manual_end; uint8_t data[640]; };
static constexpr size_t WAKE_TX_AUDIO_CAPACITY = 400;
static constexpr size_t WAKE_TX_BATCH_BYTES = 3840;
struct HostQueue { std::deque<WakeTxChunk> items; size_t capacity = WAKE_TX_AUDIO_CAPACITY + 1; };
using QueueHandle_t = HostQueue *;
static HostQueue host_tx_queue;
static QueueHandle_t g_wake_tx_queue = &host_tx_queue;
static QueueHandle_t g_audio_queue;
static std::atomic<uint32_t> g_generation{0};
static std::atomic<bool> g_abort_requested{false};
static void note_playback_stopped() { g_speaking = false; }
static std::atomic<uint32_t> g_wake_tx_generation{0};
static std::atomic<bool> g_wake_tx_enabled{false};
static std::atomic<bool> g_wake_tx_ready{false};
static std::atomic<bool> g_wake_tx_failed{false};
static std::atomic<bool> g_wake_tx_inflight{false};
static bool g_wake_tx_release_waiting;
static bool g_wake_tx_copying_prefix;
static uint32_t g_wake_tx_queued_bytes;
static uint32_t g_wake_tx_sent_bytes;
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
struct AudioCapturePending { uint32_t generation; uint64_t sample_index; size_t bytes; };
static AudioCapturePending host_capture_pending;
static bool host_pending_success = true;
static bool audio_capture_pending(AudioCapturePending *pending) { *pending = host_capture_pending; return host_pending_success; }
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
constexpr int pdTRUE = 1;
static int xQueueReset(QueueHandle_t queue) { queue->items.clear(); return pdTRUE; }
static size_t uxQueueSpacesAvailable(QueueHandle_t queue) { return queue->capacity - queue->items.size(); }
static int xQueueSend(QueueHandle_t queue, const WakeTxChunk *chunk, int) { if (!uxQueueSpacesAvailable(queue)) return 0; queue->items.push_back(*chunk); return pdTRUE; }
static int xQueuePeek(QueueHandle_t queue, WakeTxChunk *chunk, int) { if (queue->items.empty()) return 0; *chunk = queue->items.front(); return pdTRUE; }
static int xQueueReceive(QueueHandle_t queue, WakeTxChunk *chunk, int) { if (!xQueuePeek(queue, chunk, 0)) return 0; queue->items.pop_front(); return pdTRUE; }
struct AudioCaptureFrame { bool vad_valid; bool voice_active; bool continuous; uint32_t generation; uint64_t sample_index; };
static bool finish_wake_clip();
static bool queue_wake_audio(const uint8_t *, size_t);
static void abort_wake_audio();
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM };
static bool g_wifi_active_requested;
static bool g_wifi_original_ps_known;
static wifi_ps_type_t g_wifi_original_ps;
static wifi_ps_type_t host_wifi_ps = WIFI_PS_MIN_MODEM;
static int host_wifi_get_result;
static int host_wifi_set_result;
static int host_wifi_get_calls;
static int host_wifi_set_calls;
static int64_t host_now_us = 1000000;
static int64_t esp_timer_get_time() { return host_now_us; }
static int esp_wifi_get_ps(wifi_ps_type_t *mode) { ++host_wifi_get_calls; *mode = host_wifi_ps; return host_wifi_get_result; }
static int esp_wifi_set_ps(wifi_ps_type_t mode) { ++host_wifi_set_calls; if (!host_wifi_set_result) host_wifi_ps = mode; return host_wifi_set_result; }
static std::atomic<bool> g_media_active{false};
static std::atomic<bool> g_media_wake_pending{false};
static std::atomic<bool> g_chime_playing{false};
enum class MediaCommandKind : uint8_t { Manual };
struct MediaCommand { MediaCommandKind kind; uint32_t manual_request; };
static std::vector<MediaCommand> host_media_commands;
static bool host_media_command_success = true;
static std::function<void()> host_during_media_command;
static int host_capture_discard_calls;
static void audio_capture_discard_pending() { ++host_capture_discard_calls; }
static bool queue_media_command(const MediaCommand &command) { if (!host_media_command_success) return false; host_media_commands.push_back(command); if (host_during_media_command) host_during_media_command(); return true; }
static bool host_ota_busy;
static bool ota_update_busy() { return host_ota_busy; }
static void *g_ws = reinterpret_cast<void *>(1);
static bool connected = true;
static std::vector<std::vector<uint8_t>> sent;
static std::vector<std::string> log_formats;
static int failing_call = -1;
static int failure_kind;
static int checks;
static int cases;
static int failures;
static int session_end_calls;
static int wake_detect_calls;
static int listen_start_calls;
static int manual_stop_calls;
static uint32_t manual_stop_bytes;
static std::string manual_start_id;
static std::string manual_stop_id;
static std::vector<std::string> protocol_order;
static bool host_manual_ready_on_start;
static std::function<void()> host_during_control;
static int wake_reset_calls;
static int synchronous_release;
static bool end_send_success = true;
static std::string last_end_wake_id;
static int clip_end_calls;
static uint32_t clip_end_bytes;
static std::string clip_end_reason;
static bool clip_end_success = true;
static std::function<void()> host_during_send;
static size_t clip_pcm_bytes_before_marker;
static std::vector<std::pair<std::string, uint32_t>> clip_timings;

static void confirm_wake_audio_release();
struct cJSON { std::string type; std::string state; std::string wake_id; std::string reason; std::string manual_id; uint32_t audio_bytes = 0; std::vector<std::pair<std::string, uint32_t>> timings; };
static cJSON *cJSON_CreateObject() { return new cJSON; }
static void cJSON_AddStringToObject(cJSON *object, const char *key, const char *value) {
    if (std::strcmp(key, "type") == 0) object->type = value;
    if (std::strcmp(key, "state") == 0) object->state = value;
    if (std::strcmp(key, "wake_id") == 0) object->wake_id = value;
    if (std::strcmp(key, "manual_id") == 0) object->manual_id = value;
    if (std::strcmp(key, "reason") == 0) object->reason = value;
}
static void cJSON_AddNumberToObject(cJSON *object, const char *key, uint32_t value) { if (std::strcmp(key, "audio_bytes") == 0) object->audio_bytes = value; else object->timings.emplace_back(key, value); }
static bool send_json(cJSON *object) {
    if (object->type == "session_end") { ++session_end_calls; last_end_wake_id = object->wake_id; }
    if (object->type == "listen" && object->state == "detect") ++wake_detect_calls;
    if (object->type == "listen" && object->state == "start") {
        ++listen_start_calls; manual_start_id = object->manual_id; protocol_order.push_back("start");
        if (!object->manual_id.empty() && host_manual_ready_on_start) g_wake_tx_ready = true;
    }
    if (object->type == "listen" && object->state == "stop") { ++manual_stop_calls; manual_stop_id = object->manual_id; manual_stop_bytes = object->audio_bytes; protocol_order.push_back("stop"); }
    if (host_during_control) host_during_control();
    const bool clip = object->type == "wake_clip_end";
    if (clip) { ++clip_end_calls; clip_end_bytes = object->audio_bytes; clip_end_reason = object->reason; clip_timings = object->timings; clip_pcm_bytes_before_marker = 0; for (const auto &chunk : sent) clip_pcm_bytes_before_marker += chunk.size(); }
    delete object;
    return clip ? clip_end_success : end_send_success;
}
static void wake_word_reset() { ++wake_reset_calls; }
static void display_ui_state(UiState, const char * = nullptr) {}
static void display_ui_nav_icon(uint8_t) {}
static bool audio_echo_cancellation_ready() { return true; }
static void control_protocol_abort() { g_speaking = false; g_listening = false; }

static void record_log(const char *format) { log_formats.emplace_back(format); }
#define ESP_LOGI(tag, format, ...) do { (void)(tag); record_log(format); } while (0)
#define ESP_LOGW(tag, format, ...) do { (void)(tag); record_log(format); } while (0)
#define ESP_LOGE(tag, format, ...) do { (void)(tag); record_log(format); } while (0)
#define pdMS_TO_TICKS(value) (value)

static bool esp_websocket_client_is_connected(void *) { return connected; }
static int esp_websocket_client_send_bin(void *, const char *data, int length, int timeout) {
    if (length < 1 || length > 32000 || timeout != 1000) throw std::runtime_error("invalid_chunk_or_timeout");
    const int call = static_cast<int>(sent.size());
    if (host_during_send) host_during_send();
    protocol_order.push_back("pcm");
    sent.emplace_back(reinterpret_cast<const uint8_t *>(data), reinterpret_cast<const uint8_t *>(data) + length);
    if (call == failing_call) {
        // 模拟ESP WS在send返回前同步派发既有idle/disconnected确认。
        if (synchronous_release) {
            if (synchronous_release == 2) connected = false;
            g_listening = false; g_auto_session = false; g_cloud_ready = false;
            g_wake_reset_pending = true;
            confirm_wake_audio_release();
        }
        return failure_kind == 0 ? -1 : failure_kind == 1 ? 0 : length - 1;
    }
    return length;
}

// ACTUAL_WAKE_FUNCTIONS

static void require(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

static std::vector<uint8_t> pattern(size_t length, uint8_t seed) {
    std::vector<uint8_t> bytes(length);
    for (size_t index = 0; index < length; ++index) bytes[index] = static_cast<uint8_t>(seed + index * 37 + index / 251);
    return bytes;
}

static void feed(const std::vector<uint8_t> &bytes) {
    for (size_t offset = 0; offset < bytes.size(); offset += 640) {
        const size_t length = std::min<size_t>(640, bytes.size() - offset);
        cache_wake_audio(bytes.data() + offset, length);
    }
}

static void prepare_manual() {
    connected = true; g_ws = reinterpret_cast<void *>(1);
    sent.clear(); protocol_order.clear(); host_media_commands.clear(); host_during_control = {}; host_during_send = {};
    failing_call = -1; failure_kind = 0; synchronous_release = 0; session_end_calls = listen_start_calls = manual_stop_calls = 0;
    end_send_success = true; manual_start_id.clear(); manual_stop_id.clear(); manual_stop_bytes = 0;
    g_generation = 0; g_abort_requested = false; g_manual_held = false; g_manual_request = 0; g_manual_owner_request = 0; g_manual_cancel_requested = false;
    g_manual_media_waiting = false; host_capture_pending = {}; host_pending_success = true; g_manual_media_ready = 0; g_manual_media_request = 0; host_during_media_command = {}; host_capture_discard_calls = 0; host_media_command_success = true; host_manual_ready_on_start = false;
    g_wake_upload_blocked = false; g_wake_upload_pending = false; g_wake_upload_release_pending = false; g_wake_reset_pending = false;
    g_wake_tx_inflight = false; g_cloud_ready = false; g_listening = false; g_auto_session = false; g_speaking = false;
    g_media_active = false; g_media_wake_pending = false; g_chime_playing = false;
    g_manual_input_ready_supported = true; host_now_us = 1000000;
    reset_wake_audio_cache();
}

static std::vector<uint8_t> prepare(bool wrapped = false, bool finite = false, bool queued = false) {
    // 旧窄函数场景直接构造同步helper状态；queued场景覆盖生产snapshot的新入口。
    g_wake_clip_supported = queued;
    host_during_send = {};
    connected = true;
    g_ws = reinterpret_cast<void *>(1);
    sent.clear(); log_formats.clear(); failing_call = -1; synchronous_release = 0;
    g_wake_upload_pending = false; g_wake_upload_blocked = false;
    g_wake_upload_release_pending = false; g_wake_reset_pending = false;
    g_listening = false; g_auto_session = false; g_cloud_ready = false;
    session_end_calls = wake_detect_calls = listen_start_calls = wake_reset_calls = 0;
    last_end_wake_id.clear(); end_send_success = true;
    clip_end_calls = 0; clip_end_success = true; clip_end_bytes = 0; clip_end_reason.clear();
    reset_wake_audio_cache();
    const auto prefix = pattern(WAKE_PREFIX_BYTES + (wrapped ? 10000 : 0), 17);
    feed(prefix);
    snapshot_wake_prefix();
    if (!queued) g_wake_clip_active = finite;
    const auto suffix = pattern(WAKE_CACHE_BYTES + (wrapped ? 11000 : 0), 201);
    feed(suffix);
    std::vector<uint8_t> expected(prefix.end() - WAKE_PREFIX_BYTES, prefix.end());
    expected.insert(expected.end(), queued ? suffix.begin() : suffix.end() - WAKE_CACHE_BYTES, suffix.end());
    sent.clear(); log_formats.clear();
    return expected;
}

static std::vector<uint8_t> sent_bytes() {
    std::vector<uint8_t> bytes;
    for (const auto &chunk : sent) bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    return bytes;
}

static bool success_logged() {
    return std::any_of(log_formats.begin(), log_formats.end(), [](const std::string &value) {
        return value.find("wake_audio_upload prefix_bytes=") == 0;
    });
}

static void ready_queued_upload() {
    g_listening = true; g_cloud_ready = true; g_wake_upload_pending = true;
    require(upload_pending_wake_audio(), "queued_ready_failed");
}

static void drain_queued_upload() {
    for (int count = 0; count < 1000 && service_wake_audio_tx(); ++count) {}
    require(g_wake_tx_queue->items.empty(), "queued_upload_did_not_drain");
}

static void run_case(const std::string &name, const std::function<void()> &body) {
    ++cases;
    try {
        body();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
        ++failures;
        std::cout << "FAIL " << name << " " << error.what() << '\n';
    }
}

int main() {
    run_case("wifi_active_states_restore_original_mode_without_repeating_calls", [] {
        g_listening = g_speaking = g_auto_session = g_cloud_ready = g_media_active = g_media_wake_pending = g_chime_playing = false;
        g_wifi_active_requested = false; g_wifi_original_ps_known = false;
        host_wifi_ps = WIFI_PS_MAX_MODEM;
        host_wifi_get_calls = host_wifi_set_calls = 0;
        service_active_wifi_performance();
        require(host_wifi_set_calls == 0, "idle_changed_wifi_mode");
        for (auto *state : {&g_listening, &g_speaking, &g_auto_session, &g_cloud_ready, &g_media_active, &g_media_wake_pending, &g_chime_playing}) {
            *state = true;
            service_active_wifi_performance();
            require(host_wifi_ps == WIFI_PS_NONE, "active_state_kept_power_save");
            const int calls = host_wifi_set_calls;
            service_active_wifi_performance();
            require(host_wifi_set_calls == calls, "steady_state_repeated_wifi_calls");
            *state = false;
            service_active_wifi_performance();
            require(host_wifi_ps == WIFI_PS_MAX_MODEM, "idle_did_not_restore_original");
        }
        host_ota_busy = true; service_active_wifi_performance();
        require(host_wifi_ps == WIFI_PS_NONE, "ota_state_kept_power_save");
        host_ota_busy = false; service_active_wifi_performance();
        require(host_wifi_ps == WIFI_PS_MAX_MODEM && host_wifi_get_calls == 1, "original_mode_recaptured_or_lost");
    });
    run_case("wifi_failure_never_interrupts_or_creates_retry_loop", [] {
        g_wifi_active_requested = false; g_wifi_original_ps_known = false;
        host_wifi_ps = WIFI_PS_MIN_MODEM;
        host_wifi_get_result = -1;
        const int calls = host_wifi_get_calls;
        set_active_wifi_performance(true); set_active_wifi_performance(true);
        require(host_wifi_get_calls == calls + 1 && host_wifi_ps == WIFI_PS_MIN_MODEM, "read_failure_retried_or_changed_mode");
        set_active_wifi_performance(false);
        host_wifi_get_result = 0; host_wifi_set_result = -2;
        set_active_wifi_performance(true);
        require(host_wifi_ps == WIFI_PS_MIN_MODEM, "failed_set_changed_mode");
        const int sets = host_wifi_set_calls;
        set_active_wifi_performance(true);
        require(host_wifi_set_calls == sets, "failed_set_retried_in_steady_state");
        host_wifi_set_result = 0; set_active_wifi_performance(false);
        require(host_wifi_ps == WIFI_PS_MIN_MODEM, "failure_path_lost_original_mode");
    });
    run_case("success_prefix_then_suffix_exact_bytes", [] {
        const auto expected = prepare();
        upload_wake_audio(); // 忽略返回以兼容旧void与候选bool；按实际发送行为判定。
        require(sent_bytes() == expected, "prefix_suffix_byte_order_changed");
        require(sent.size() == 6, "normal_chunk_count_changed");
        for (const auto &chunk : sent) require(chunk.size() == 32000, "normal_chunk_boundary_changed");
        require(success_logged(), "complete_upload_not_logged");
        require(g_wake_prefix_count == 0 && g_wake_cache_count == 0 && !g_wake_prefix_frozen, "success_did_not_clear_cache");
    });
    run_case("wrapped_prefix_and_suffix_preserve_order", [] {
        const auto expected = prepare(true);
        upload_wake_audio();
        require(sent_bytes() == expected, "wrapped_ring_order_changed");
        const std::vector<size_t> lengths = {32000,32000,32000,22000,10000,32000,21000,11000};
        require(sent.size() == lengths.size(), "wrapped_chunk_count_changed");
        for (size_t index = 0; index < lengths.size(); ++index) require(sent[index].size() == lengths[index], "wrapped_chunk_boundary_changed");
        require(success_logged(), "wrapped_success_not_logged");
    });
    run_case("freeze_keeps_prefix_and_resets_suffix", [] {
        prepare();
        require(g_wake_prefix_frozen, "prefix_not_frozen");
        require(g_wake_prefix_count == WAKE_PREFIX_BYTES && g_wake_cache_count == WAKE_CACHE_BYTES, "bounded_cache_counts_changed");
        const auto prefix_before = std::vector<uint8_t>(prefix_storage, prefix_storage + WAKE_PREFIX_BYTES);
        feed(pattern(640, 89));
        require(std::equal(prefix_before.begin(), prefix_before.end(), prefix_storage), "postwake_audio_overwrote_prefix");
    });
    run_case("disconnected_upload_does_not_send_or_claim_success", [] {
        prepare(); connected = false;
        upload_wake_audio();
        require(sent.empty(), "disconnected_upload_sent");
        require(!success_logged(), "disconnected_upload_claimed_success");
        require(g_wake_prefix_count == WAKE_PREFIX_BYTES, "disconnected_upload_lost_prefix");
    });
    run_case("empty_upload_does_not_claim_success", [] {
        prepare(); reset_wake_audio_cache();
        upload_wake_audio();
        require(sent.empty() && !success_logged(), "empty_upload_wrote_or_claimed_success");
    });
    for (const auto &position : std::vector<std::pair<const char *,int>>{{"first",0},{"middle",2},{"suffix",4}}) {
        for (int kind = 0; kind < 3; ++kind) {
            const std::string name = std::string("failure_") + position.first + (kind == 0 ? "_negative" : kind == 1 ? "_zero" : "_short");
            run_case(name, [position,kind] {
                prepare(); failing_call = position.second; failure_kind = kind;
                const auto prefix_count = g_wake_prefix_count;
                const auto suffix_count = g_wake_cache_count;
                upload_wake_audio();
                std::cout << "OBSERVED failed_call=" << position.second << " return_kind=" << kind
                          << " actual_calls=" << sent.size() << " prefix_before=" << prefix_count
                          << " prefix_after=" << g_wake_prefix_count << " suffix_before=" << suffix_count
                          << " suffix_after=" << g_wake_cache_count << " success_logged=" << success_logged() << '\n';
                require(sent.size() == static_cast<size_t>(position.second + 1), "continued_upload_after_failed_chunk");
                require(!success_logged(), "failed_upload_claimed_success");
                require(g_wake_prefix_count == prefix_count && g_wake_cache_count == suffix_count, "failed_upload_cleared_snapshot_as_success");
            });
        }
    }
    run_case("pending_success_once_then_live_may_continue", [] {
        const auto expected = prepare();
        g_wake_upload_pending = true; g_listening = true; g_cloud_ready = true; g_auto_session = true;
        require(upload_pending_wake_audio(), "successful_pending_upload_prevented_live");
        require(sent_bytes() == expected, "pending_success_byte_order_changed");
        require(!g_wake_upload_pending.load() && !g_wake_upload_blocked.load(), "successful_pending_not_consumed");
        const auto calls = sent.size();
        require(upload_pending_wake_audio(), "no_pending_should_preserve_live");
        require(sent.size() == calls && session_end_calls == 0, "successful_prefill_was_replayed");
    });
    run_case("pending_failure_blocks_live_retry_cache_and_new_entries_until_release", [] {
        prepare(); failing_call = 1; failure_kind = 2;
        const std::string wake_id = g_wake_id;
        g_wake_upload_pending = true; g_listening = true; g_cloud_ready = true; g_auto_session = true;
        require(!upload_pending_wake_audio(), "failed_pending_allowed_live");
        require(g_wake_upload_blocked.load() && !g_wake_upload_pending.load(), "failed_candidate_not_isolated");
        require(!g_listening.load() && !g_cloud_ready.load() && !g_auto_session.load(), "failed_candidate_still_active");
        require(std::string(g_wake_id) == wake_id && last_end_wake_id == wake_id, "failed_candidate_identity_lost");
        require(session_end_calls == 1, "session_end_not_single_attempt");
        const auto calls = sent.size();
        const auto prefix_count = g_wake_prefix_count;
        const auto suffix_count = g_wake_cache_count;
        cache_wake_audio(prefix_storage, 640); snapshot_wake_prefix(); send_wake_detect(); begin_listening(true);
        require(!accept_wake_listening(), "late_listening_reenabled_failed_candidate");
        require(!upload_pending_wake_audio(), "isolated_candidate_allowed_live_or_retry");
        require(sent.size() == calls && session_end_calls == 1, "isolated_candidate_retried_network");
        require(g_wake_prefix_count == prefix_count && g_wake_cache_count == suffix_count && std::string(g_wake_id) == wake_id, "failed_cache_was_reused");
        require(wake_detect_calls == 0 && listen_start_calls == 0, "new_entry_started_before_release");
        g_wake_reset_pending = true;
        require(!service_wake_audio_reset(), "generic_reset_bypassed_release_confirmation");
        require(g_wake_upload_blocked.load() && std::string(g_wake_id) == wake_id, "failed_identity_released_early");
        confirm_wake_audio_release();
        require(service_wake_audio_reset(), "confirmed_release_did_not_reset");
        require(!g_wake_upload_blocked.load() && g_wake_prefix_count == 0 && g_wake_cache_count == 0 && g_wake_id[0] == '\0', "release_kept_dirty_snapshot");
        require(wake_reset_calls == 1, "wake_model_reset_not_once");
        cache_wake_audio(prefix_storage, 640); snapshot_wake_prefix();
        require(std::string(g_wake_id) != wake_id, "next_candidate_reused_old_identity");
        send_wake_detect(); begin_listening(true);
        require(wake_detect_calls == 1 && listen_start_calls == 1, "confirmed_release_did_not_restore_entries");
        require(accept_wake_listening() && g_cloud_ready.load(), "next_listening_not_accepted");
    });
    for (int release : {1,2}) {
        run_case(release == 1 ? "synchronous_idle_before_send_returns_is_not_lost" : "synchronous_disconnect_before_send_returns_is_not_lost", [release] {
            prepare(); failing_call = 0; failure_kind = 0; synchronous_release = release;
            const std::string wake_id = g_wake_id;
            g_wake_upload_pending = true; g_listening = true; g_cloud_ready = true; g_auto_session = true;
            require(!upload_pending_wake_audio(), "sync_release_allowed_live");
            require(g_wake_upload_blocked.load() && g_wake_upload_release_pending.load(), "sync_release_confirmation_lost");
            require(std::string(g_wake_id) == wake_id, "sync_release_cleared_id_before_capture_cycle");
            require(sent.size() == 1 && session_end_calls == 0, "sync_release_sent_after_session_end");
            require(service_wake_audio_reset(), "next_cycle_did_not_consume_sync_release");
            require(!g_wake_upload_blocked.load() && g_wake_id[0] == '\0' && g_wake_prefix_count == 0 && g_wake_cache_count == 0, "sync_release_stuck_or_dirty");
        });
    }
    run_case("unconfirmed_session_end_keeps_safe_isolation_without_retry", [] {
        prepare(); failing_call = 0; failure_kind = 0; end_send_success = false;
        g_wake_upload_pending = true; g_listening = true; g_cloud_ready = true;
        require(!upload_pending_wake_audio(), "unconfirmed_end_allowed_live");
        for (int iteration = 0; iteration < 3; ++iteration) {
            require(!upload_pending_wake_audio() && !service_wake_audio_reset(), "unconfirmed_end_faked_recovery");
        }
        require(session_end_calls == 1 && sent.size() == 1 && g_wake_upload_blocked.load(), "unconfirmed_end_retried_or_resumed");
    });
    run_case("release_already_pending_before_upload_is_not_erased", [] {
        prepare(); confirm_wake_audio_release();
        g_wake_upload_pending = true; g_listening = true; g_cloud_ready = true;
        require(!upload_pending_wake_audio(), "known_ended_candidate_allowed_live");
        require(sent.empty() && session_end_calls == 0 && g_wake_upload_release_pending.load(), "known_release_erased_or_old_audio_sent");
        require(service_wake_audio_reset() && !g_wake_upload_blocked.load(), "known_release_did_not_recover");
    });
    run_case("clip_silence_boundary_once_and_inline_command_preserved", [] {
        const auto expected = prepare(false, true);
        require(upload_wake_audio(), "clip_prefill_failed");
        require(g_wake_clip_sent_bytes == expected.size(), "clip_prefill_bytes_wrong");
        note_wake_clip_audio(16000 * 2 / 5, false);
        require(finish_wake_clip() && clip_end_calls == 0, "short_pause_ended_inline_command");
        note_wake_clip_audio(640, true);
        require(g_wake_clip_quiet_samples == 0, "inline_command_did_not_reset_silence");
        note_wake_clip_audio(16000 * 2 * 300 / 1000, false);
        const auto current = pattern(640, 12);
        require(send_capture_audio(current.data(), current.size()), "final_clip_frame_failed");
        require(clip_end_calls == 1 && clip_end_reason == "silence", "clip_boundary_missing");
        require(clip_end_bytes == expected.size() + current.size(), "clip_boundary_excluded_last_frame");
        require(finish_wake_clip() && clip_end_calls == 1, "clip_boundary_repeated");
    });
    run_case("clip_hard_limit_marks_incomplete_instead_of_silent_cut", [] {
        prepare(false, true); require(upload_wake_audio(), "clip_upload_failed");
        note_wake_clip_audio(WAKE_CLIP_LIMIT_SAMPLES * 2, true);
        require(finish_wake_clip() && clip_end_calls == 1 && clip_end_reason == "limit", "missing_limit_reason");
    });
    run_case("wake_device_timing_uses_local_ready_and_first_actual_upload", [] {
        host_now_us = 1000000;
        prepare(false, true);
        host_now_us += 340000; require(accept_wake_listening(), "ready_not_accepted");
        host_now_us += 2000; require(upload_wake_audio(), "timed_upload_failed");
        host_now_us += 400000; note_wake_clip_audio(16000, false);
        require(finish_wake_clip(), "timed_clip_failed");
        require(clip_timings == std::vector<std::pair<std::string, uint32_t>>{{"device_detect_to_ready_ms",340},{"device_ready_to_first_upload_ms",2},{"device_detect_to_end_ms",742}}, "local_timing_boundaries_wrong");
        reset_wake_audio_cache();
        require(!g_wake_detected_us && !g_wake_cloud_ready_us && !g_wake_first_upload_us, "old_timing_survived_reset");
    });
    run_case("clip_connect_buffer_overflow_never_overwrites_command", [] {
        prepare(false, true);
        const auto before = std::vector<uint8_t>(suffix_storage, suffix_storage + WAKE_CACHE_BYTES);
        feed(pattern(640, 44));
        require(g_wake_cache_overflow, "bounded_cache_overflow_not_detected");
        require(std::equal(before.begin(), before.end(), suffix_storage), "command_audio_overwritten");
        g_wake_upload_pending = true; g_listening = true; g_cloud_ready = true;
        require(!upload_pending_wake_audio() && sent.empty() && g_wake_upload_blocked, "overflow_audio_uploaded");
    });
    run_case("clip_live_short_write_aborts_without_boundary_or_retry", [] {
        prepare(false, true); require(upload_wake_audio(), "clip_upload_failed");
        failing_call = static_cast<int>(sent.size()); failure_kind = 2;
        const auto current = pattern(640, 8);
        require(!send_capture_audio(current.data(), current.size()), "short_write_accepted");
        require(clip_end_calls == 0 && g_wake_upload_blocked && session_end_calls == 1, "short_write_not_isolated");
    });
    run_case("clip_boundary_failure_isolated_until_release", [] {
        prepare(false, true); require(upload_wake_audio(), "clip_upload_failed");
        note_wake_clip_audio(16000, false); clip_end_success = false;
        require(!finish_wake_clip() && g_wake_upload_blocked && session_end_calls == 1, "boundary_failure_not_isolated");
        confirm_wake_audio_release(); require(service_wake_audio_reset(), "clip_release_failed");
        require(!g_wake_clip_active && !g_wake_cache_overflow && g_wake_clip_sent_bytes == 0, "clip_state_survived_reset");
    });
    run_case("legacy_peer_keeps_original_stream_without_clip_boundary", [] {
        prepare(); note_wake_clip_audio(16000 * 20, false);
        require(finish_wake_clip() && clip_end_calls == 0 && !g_wake_clip_active, "legacy_peer_changed");
    });
    run_case("queued_wrapped_prefix_connection_suffix_and_live_remain_exact", [] {
        const auto expected = prepare(true, true, true);
        require(sent.empty() && !service_wake_audio_tx(), "queue_sent_before_ready");
        ready_queued_upload(); drain_queued_upload();
        require(sent_bytes() == expected, "queued_wrapped_order_changed");
        for (const auto &chunk : sent) require(chunk.size() <= WAKE_TX_BATCH_BYTES, "queued_message_exceeded_single_fragment");
    });
    run_case("queued_boundary_between_last_clip_frame_and_live_continuation", [] {
        auto expected = prepare(false, true, true);
        const auto last = pattern(640, 19);
        note_wake_clip_audio(9600, false);
        require(send_capture_audio(last.data(), last.size()), "last_clip_enqueue_failed");
        expected.insert(expected.end(), last.begin(), last.end());
        const size_t clip_size = expected.size();
        const auto continuation = pattern(1280, 87);
        require(send_capture_audio(continuation.data(), continuation.size()), "continuation_enqueue_failed");
        expected.insert(expected.end(), continuation.begin(), continuation.end());
        ready_queued_upload(); drain_queued_upload();
        require(clip_end_calls == 1 && clip_end_bytes == clip_size && clip_pcm_bytes_before_marker == clip_size, "marker_not_after_exact_clip");
        require(sent_bytes() == expected, "continuation_lost_or_reordered");
    });
    run_case("queued_full_aborts_without_silent_overwrite_or_sync_fallback", [] {
        prepare(false, true, true);
        const auto frame = pattern(640, 73);
        while (!g_wake_upload_blocked.load()) (void)send_capture_audio(frame.data(), frame.size());
        require(!g_wake_tx_enabled && sent.empty() && clip_end_calls == 0 && session_end_calls == 1, "queue_full_not_isolated");
        require(!service_wake_audio_tx() && g_wake_tx_queue->items.empty(), "aborted_queue_sent_later");
    });
    run_case("idle_during_inflight_send_waits_before_new_wake_generation", [] {
        prepare(false, true, true); ready_queued_upload();
        host_during_send = [] {
            confirm_wake_audio_release(); g_listening = false; g_cloud_ready = false;
            require(!service_wake_audio_reset() && g_wake_upload_blocked, "reset_passed_inflight_owner");
            const uint32_t identity = g_next_wake_id; snapshot_wake_prefix();
            require(g_next_wake_id == identity, "new_wake_started_before_old_send_exit");
        };
        require(service_wake_audio_tx(), "inflight_case_did_not_send");
        host_during_send = {};
        require(!g_wake_tx_inflight && service_wake_audio_reset() && !g_wake_upload_blocked, "owner_exit_did_not_release_capture");
        const auto fresh = pattern(2560, 143); feed(fresh); g_wake_clip_supported = true; snapshot_wake_prefix();
        const size_t old_size = sent_bytes().size();
        ready_queued_upload(); drain_queued_upload();
        auto actual = sent_bytes();
        require(std::vector<uint8_t>(actual.begin() + old_size, actual.end()) == fresh, "old_audio_entered_new_candidate");
    });
    run_case("tts_wakenet_reset_preserves_pending_live_queue", [] {
        prepare(false, true, true); ready_queued_upload();
        const size_t count = g_wake_tx_queue->items.size();
        g_wake_reset_pending = true;
        require(service_wake_audio_reset() && g_wake_tx_enabled && g_wake_tx_queue->items.size() == count, "tts_reset_erased_live_audio");
    });
    run_case("wall_limit_ends_once_even_without_new_pcm_samples", [] {
        host_now_us = 1000000; prepare(false, true, true);
        host_now_us = g_wake_detected_us.load() + 8000000;
        require(g_wake_clip_samples == 0 && finish_wake_clip() && !g_wake_clip_active, "wall_limit_waited_for_consumed_samples");
        const size_t count = g_wake_tx_queue->items.size();
        require(finish_wake_clip() && g_wake_tx_queue->items.size() == count, "wall_limit_repeated_marker");
        ready_queued_upload(); drain_queued_upload();
        require(clip_end_calls == 1 && clip_end_reason == "limit", "wall_cut_was_authorized_as_complete");
    });
    run_case("slow_sender_allows_capture_owner_to_queue_every_pcm_frame", [] {
        host_now_us = 1000000;
        auto expected = prepare(false, true, true); ready_queued_upload();
        uint32_t produced_samples = 0;
        uint32_t afe_available = 0;
        uint32_t afe_lost = 0;
        host_during_send = [&] {
            for (int step = 0; step < 15; ++step) {
                host_now_us += 20000; afe_available += 320;
                if (afe_available > 4096) afe_lost += afe_available - 4096;
                const auto frame = pattern(640, static_cast<uint8_t>(step));
                note_wake_clip_audio(frame.size(), true);
                require(send_capture_audio(frame.data(), frame.size()), "capture_blocked_by_slow_sender");
                expected.insert(expected.end(), frame.begin(), frame.end());
                produced_samples += afe_available; afe_available = 0;
            }
        };
        require(service_wake_audio_tx(), "slow_sender_not_entered");
        host_during_send = {};
        require(produced_samples == 4800 && afe_lost == 0, "decoupled_consumer_lost_afe_samples");
        drain_queued_upload();
        require(sent_bytes() == expected, "slow_send_changed_audio_bytes");
        require(g_wake_tx_max_us == 300000 && g_wake_first_upload_done_us > g_wake_first_upload_us, "send_completion_timing_missing");
    });
    run_case("queued_short_write_signals_capture_abort_and_never_falls_back", [] {
        prepare(false, true, true); ready_queued_upload();
        failing_call = 0; failure_kind = 2;
        require(service_wake_audio_tx() && g_wake_tx_failed && !g_wake_tx_ready && !service_wake_audio_tx(), "short_write_not_reported_or_sender_continued");
        if (g_wake_tx_failed.exchange(false)) abort_wake_audio();
        require(g_wake_upload_blocked && !g_wake_tx_enabled && !service_wake_audio_tx() && session_end_calls == 1, "sender_failure_not_isolated");
    });
    run_case("missing_queue_aborts_instead_of_synchronous_fallback", [] {
        prepare(); g_wake_clip_supported = true; g_wake_tx_queue = nullptr;
        snapshot_wake_prefix();
        require(g_wake_upload_blocked && !g_wake_tx_enabled && sent.empty(), "allocation_failure_fell_back_to_sync");
        g_wake_tx_queue = &host_tx_queue;
    });
    run_case("early_upload_timing_remains_visible_before_cloud_ready", [] {
        host_now_us = 1000000; prepare(false, true, true);
        g_wake_input_ready_us = host_now_us + 50000;
        host_now_us += 60000;
        g_wake_tx_ready = true;
        require(service_wake_audio_tx(), "early_transport_ready_did_not_send");
        host_now_us += 100000;
        note_wake_clip_audio(9600, false); require(finish_wake_clip(), "early_boundary_failed");
        drain_queued_upload();
        const auto find = [](const std::string &key) {
            auto found = std::find_if(clip_timings.begin(), clip_timings.end(), [&](const auto &item) { return item.first == key; });
            return found == clip_timings.end() ? UINT32_MAX : found->second;
        };
        require(find("device_detect_to_input_ready_ms") == 50 && find("device_input_ready_to_first_upload_ms") == 10 && find("device_detect_to_first_upload_ms") == 60, "early_timing_omitted_or_wrong");
        require(find("device_detect_to_ready_ms") == UINT32_MAX && !g_cloud_ready.load(), "transport_ready_faked_cloud_ready");
    });

    run_case("manual_release_before_ready_drains_exact_pcm_then_one_stop", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening();
        const auto pcm = pattern(3300, 67); require(capture_manual_audio(pcm.data(), pcm.size()), "manual_not_captured");
        control_protocol_listen_stop(); service_manual_listening();
        require(!g_listening && g_manual_ended && sent.empty() && manual_stop_calls == 0, "release_committed_before_ready");
        require(service_wake_audio_tx() && listen_start_calls == 1 && !service_wake_audio_tx(), "start_not_before_ready_or_pcm_escaped");
        g_wake_tx_ready = true; drain_queued_upload();
        require(sent_bytes() == pcm && manual_stop_calls == 1 && manual_stop_bytes == pcm.size(), "manual_tail_lost");
        require(protocol_order.front() == "start" && protocol_order.back() == "stop" && manual_start_id == manual_stop_id, "manual_protocol_order_wrong");
        control_protocol_listen_stop(); service_manual_listening(); drain_queued_upload();
        require(manual_stop_calls == 1, "duplicate_stop_queued");
        require(capture_manual_audio(pcm.data(), pcm.size()) && g_wake_tx_queue->items.empty(), "released_turn_captured_new_audio");
    });

    run_case("manual_release_drains_only_frozen_afe_ring_tail_before_stop", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true; service_wake_audio_tx();
        auto expected = pattern(640, 37); AudioCaptureFrame metadata = {true, true, false, 4, 100};
        capture_manual_audio(expected.data(), expected.size(), &metadata);
        host_capture_pending = {4, 420, 960}; control_protocol_listen_stop(); service_manual_listening();
        require(g_manual_releasing && !g_manual_ended && manual_stop_calls == 0, "release_dropped_unread_afe_tail");
        const auto tail = pattern(640, 69); metadata.continuous = true; metadata.sample_index = 420;
        capture_manual_audio(tail.data(), tail.size(), &metadata); expected.insert(expected.end(), tail.begin(), tail.end()); service_manual_listening();
        require(!g_manual_ended && g_manual_pending.bytes == 320, "tail_snapshot_drained_too_early");
        metadata.sample_index = 740; capture_manual_audio(tail.data(), tail.size(), &metadata); expected.insert(expected.end(), tail.begin(), tail.begin() + 320); service_manual_listening(); drain_queued_upload();
        require(sent_bytes() == expected && manual_stop_bytes == expected.size() && g_manual_ended, "release_tail_lost_or_post_release_pcm_added");
    });
    run_case("manual_ring_discontinuity_or_snapshot_failure_cancels_without_commit", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening();
        const auto pcm = pattern(640, 43); AudioCaptureFrame metadata = {true, true, false, 8, 0}; capture_manual_audio(pcm.data(), pcm.size(), &metadata);
        host_capture_pending = {9, 320, 640}; control_protocol_listen_stop(); service_manual_listening();
        require(g_wake_upload_blocked && manual_stop_calls == 0, "overflowed_tail_committed");
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_pending_success = false; control_protocol_listen_stop(); service_manual_listening();
        require(g_wake_upload_blocked && manual_stop_calls == 0, "failed_tail_snapshot_committed");
    });

    run_case("manual_ready_callback_rejects_old_identity_and_publishes_only_current_token", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); service_wake_audio_tx();
        require(accept_manual_input_ready("manual-1") && !g_wake_tx_ready, "callback_bypassed_owner_ready_gate");
        g_manual_cancel_requested = true; service_manual_listening(); confirm_wake_audio_release(); service_wake_audio_reset();
        control_protocol_listen_stop(); control_protocol_listen_start(); service_manual_listening(); service_wake_audio_tx();
        require(std::string(g_manual_id) == "manual-2" && !g_wake_tx_ready, "fresh_turn_not_waiting_for_own_ready");
        require(!accept_manual_input_ready("manual-1"), "old_ready_accepted_for_new_turn");
        // 旧回调若在owner切代后才完成写入，只留下旧数值token，也不能放行新代。
        g_manual_ready_ack_token = 1; service_manual_listening();
        require(!g_wake_tx_ready && !service_wake_audio_tx(), "delayed_old_callback_enabled_new_turn");
        require(accept_manual_input_ready("manual-2") && !g_wake_tx_ready, "current_ready_missing_or_directly_enabled_sender");
        service_manual_listening(); require(g_wake_tx_ready, "owner_failed_to_accept_current_ready_token");
        confirm_wake_audio_release(); service_wake_audio_reset();
        require(!accept_manual_input_ready("manual-2") && !g_wake_tx_ready, "released_turn_accepted_late_ready");
    });
    run_case("manual_zero_pcm_sends_truthful_zero_without_padding", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); control_protocol_listen_stop(); service_manual_listening();
        host_manual_ready_on_start = true; drain_queued_upload();
        require(sent.empty() && manual_stop_calls == 1 && manual_stop_bytes == 0, "zero_pcm_was_fabricated");
    });
    run_case("manual_press_release_before_owner_starts_opens_no_session", [] {
        prepare_manual(); control_protocol_listen_start(); control_protocol_listen_stop(); service_manual_listening();
        require(!g_manual_active && !service_wake_audio_tx() && listen_start_calls == 0, "released_button_started_session");
    });
    run_case("manual_media_pause_release_before_ack_never_starts", [] {
        prepare_manual(); g_media_active = true; control_protocol_listen_start(); service_manual_listening();
        require(host_media_commands.size() == 1 && host_media_commands[0].manual_request == 1 && g_media_wake_pending, "media_pause_missing_identity");
        control_protocol_listen_stop(); service_manual_listening();
        // 播放任务与回执sender的成功暂停回执；仅交付代数，不调用begin_listening。
        g_media_active = false; g_media_wake_pending = false; g_manual_media_ready = 1;
        service_manual_listening(); require(!g_manual_active && !service_wake_audio_tx(), "late_media_ack_reopened_released_turn");
        control_protocol_listen_start(); service_manual_listening();
        require(g_manual_active && std::string(g_manual_id) == "manual-2", "fresh_press_not_new_owner");
    });
    run_case("manual_media_held_waits_for_ack_and_rapid_repress_uses_new_request", [] {
        prepare_manual(); g_media_active = true; control_protocol_listen_start(); service_manual_listening();
        control_protocol_listen_stop(); control_protocol_listen_start(); service_manual_listening();
        require(!g_manual_active && listen_start_calls == 0, "rapid_repress_bypassed_media_pause");
        g_media_active = false; service_manual_listening(); require(!g_manual_active, "manual_started_before_pause_ack");
        g_media_wake_pending = false; g_manual_media_ready = 1; service_manual_listening();
        require(g_manual_active && std::string(g_manual_id) == "manual-2", "old_pause_ack_reused_old_turn");
    });
    run_case("manual_media_queue_failure_is_bounded_and_starts_nothing", [] {
        prepare_manual(); g_media_active = true; host_media_command_success = false;
        control_protocol_listen_start(); service_manual_listening(); service_manual_listening();
        require(!g_manual_active && host_media_commands.empty() && !g_manual_media_waiting, "media_queue_failure_opened_manual");
    });

    run_case("manual_media_synchronous_ack_is_not_erased_and_stale_ack_cannot_start", [] {
        prepare_manual(); g_media_active = true;
        host_during_media_command = [] { g_media_active = false; g_media_wake_pending = false; g_manual_media_ready = g_manual_media_request.load(); };
        control_protocol_listen_start(); service_manual_listening(); service_manual_listening();
        require(g_manual_active && g_manual_media_ready == 1 && host_capture_discard_calls == 1, "synchronous_media_ack_erased");
        prepare_manual(); g_media_active = true; control_protocol_listen_start(); service_manual_listening();
        g_media_active = false; g_media_wake_pending = false; g_manual_media_ready = 99; service_manual_listening();
        require(!g_manual_active && host_capture_discard_calls == 0, "stale_media_ack_started_current_request");
        g_manual_media_ready = 1; service_manual_listening(); require(g_manual_active && host_capture_discard_calls == 1, "matching_media_ack_not_accepted");
    });
    run_case("manual_unnegotiated_gateway_fails_closed", [] {
        prepare_manual(); g_manual_input_ready_supported = false; control_protocol_listen_start(); service_manual_listening();
        require(!g_manual_active && !service_wake_audio_tx() && listen_start_calls == 0, "legacy_gateway_received_new_manual_protocol");
    });
    run_case("manual_slow_send_release_keeps_inflight_and_queued_tail_before_stop", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true;
        require(service_wake_audio_tx(), "manual_start_missing");
        auto expected = pattern(640, 31); capture_manual_audio(expected.data(), expected.size());
        host_during_send = [&] {
            const auto tail = pattern(2600, 98); capture_manual_audio(tail.data(), tail.size()); expected.insert(expected.end(), tail.begin(), tail.end());
            control_protocol_listen_stop(); service_manual_listening(); host_now_us += 300000;
        };
        require(service_wake_audio_tx(), "manual_slow_send_missing"); host_during_send = {}; drain_queued_upload();
        require(sent_bytes() == expected && manual_stop_bytes == expected.size() && protocol_order.back() == "stop", "slow_manual_send_lost_tail");
    });
    run_case("manual_queue_full_and_short_write_never_commit_partial_turn", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); const auto pcm = pattern(640, 90);
        while (!g_wake_upload_blocked) capture_manual_audio(pcm.data(), pcm.size());
        require(session_end_calls == 1 && manual_stop_calls == 0 && sent.empty(), "full_manual_queue_committed");
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true; service_wake_audio_tx();
        capture_manual_audio(pcm.data(), pcm.size()); control_protocol_listen_stop(); service_manual_listening();
        failing_call = 0; failure_kind = 2; service_wake_audio_tx();
        require(g_wake_tx_failed && !service_wake_audio_tx(), "short_write_sender_continued");
        if (g_wake_tx_failed.exchange(false)) abort_wake_audio();
        require(session_end_calls == 1 && manual_stop_calls == 0, "short_manual_write_committed");
    });
    run_case("manual_cancel_inflight_and_repress_wait_for_release_barrier", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true; service_wake_audio_tx();
        const auto old_pcm = pattern(640, 11); capture_manual_audio(old_pcm.data(), old_pcm.size());
        host_during_send = [] {
            control_protocol_listen_stop(); control_protocol_listen_start(); service_manual_listening();
            confirm_wake_audio_release(); require(!service_wake_audio_reset(), "manual_release_crossed_inflight");
            service_manual_listening(); require(listen_start_calls == 1, "new_manual_started_during_old_send");
        };
        service_wake_audio_tx(); host_during_send = {};
        require(service_wake_audio_reset(), "manual_old_sender_did_not_release"); service_manual_listening();
        const auto fresh = pattern(1280, 75); capture_manual_audio(fresh.data(), fresh.size()); control_protocol_listen_stop(); service_manual_listening();
        drain_queued_upload(); const auto actual = sent_bytes();
        require(manual_stop_calls == 1 && manual_stop_bytes == fresh.size() && manual_stop_id == "manual-2", "canceled_turn_crossed_new_owner");
        require(actual.size() == old_pcm.size() + fresh.size() && std::equal(fresh.begin(), fresh.end(), actual.begin() + old_pcm.size()), "old_audio_replayed_in_fresh_turn");
    });
    run_case("manual_stage_timeouts_and_capture_limit_never_commit", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); service_wake_audio_tx(); host_now_us += 4000000; service_manual_listening();
        require(g_wake_upload_blocked && manual_stop_calls == 0 && session_end_calls == 1, "manual_ready_timeout_committed");
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true; service_wake_audio_tx();
        control_protocol_listen_stop(); service_manual_listening(); host_now_us += 8000000; service_manual_listening();
        require(g_wake_upload_blocked && manual_stop_calls == 0, "manual_drain_timeout_committed");
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true; service_wake_audio_tx();
        host_now_us += 30000000; service_manual_listening(); require(g_wake_upload_blocked && manual_stop_calls == 0, "manual_capture_limit_committed_truncated_command");
    });
    run_case("manual_start_or_stop_failure_has_no_retry_or_partial_replay", [] {
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); end_send_success = false; service_wake_audio_tx();
        require(g_wake_tx_failed && !service_wake_audio_tx() && sent.empty(), "failed_manual_start_allowed_pcm");
        prepare_manual(); control_protocol_listen_start(); service_manual_listening(); host_manual_ready_on_start = true; service_wake_audio_tx();
        control_protocol_listen_stop(); service_manual_listening(); end_send_success = false; service_wake_audio_tx();
        require(g_wake_tx_failed && !g_manual_end_sent && !service_wake_audio_tx() && manual_stop_calls == 1, "failed_manual_stop_retried");
    });
    run_case("quiet_boundary_keeps_preroll_and_entire_inline_command", [] {
        prepare_manual(); const auto background = pattern(60000, 9); feed(background);
        AudioCaptureFrame metadata = {true, false, false, 7, 0}; const auto quiet = pattern(640, 0);
        for (int index = 0; index < 25; ++index) { cache_wake_audio(quiet.data(), quiet.size()); note_wake_boundary_audio(quiet.size(), metadata); metadata.continuous = true; metadata.sample_index += 320; }
        require(g_wake_prefix_count == 9600 && g_wake_boundary_reset, "confirmed_quiet_boundary_did_not_trim");
        const auto command = pattern(3600, 139); metadata.voice_active = true;
        cache_wake_audio(command.data(), command.size()); note_wake_boundary_audio(command.size(), metadata);
        g_wake_clip_supported = false; snapshot_wake_prefix(); require(upload_wake_audio(), "quiet_boundary_upload_failed");
        const auto actual = sent_bytes(); require(actual.size() == 9600 + command.size() && std::equal(command.begin(), command.end(), actual.end() - command.size()), "quiet_boundary_cut_inline_command");
        require(std::equal(actual.begin(), actual.begin() + 640, quiet.begin()), "quiet_preroll_changed");
    });
    run_case("continuous_background_and_invalid_or_missing_frames_never_fake_boundary", [] {
        prepare_manual(); const auto pcm = pattern(640, 77); feed(pattern(50000, 3));
        AudioCaptureFrame metadata = {true, false, false, 1, 0};
        for (int index = 0; index < 24; ++index) { cache_wake_audio(pcm.data(), pcm.size()); note_wake_boundary_audio(pcm.size(), metadata); metadata.continuous = true; metadata.sample_index += 320; }
        const size_t count = g_wake_prefix_count; metadata.sample_index += 320; // 模拟其他reader消耗一帧；continuous仍true。
        cache_wake_audio(pcm.data(), pcm.size()); note_wake_boundary_audio(pcm.size(), metadata);
        require(!g_wake_boundary_reset && g_wake_prefix_count == count + 640 && g_wake_boundary_quiet_samples == 320, "sample_gap_was_trimmed_as_silence");
        metadata.sample_index += 320; metadata.vad_valid = false; note_wake_boundary_audio(pcm.size(), metadata);
        require(g_wake_boundary_quiet_samples == 0 && !g_wake_boundary_reset, "invalid_vad_kept_boundary");
        metadata.vad_valid = true; metadata.voice_active = true;
        for (int index = 0; index < 40; ++index) { metadata.sample_index += 320; cache_wake_audio(pcm.data(), pcm.size()); note_wake_boundary_audio(pcm.size(), metadata); }
        require(!g_wake_boundary_reset && g_wake_prefix_count > 9600, "continuous_voice_was_arbitrarily_cropped");
    });

    run_case("invalid_vad_keeps_original_wake_model_until_actual_discontinuity", [] {
        prepare_manual(); const auto pcm = pattern(640, 12); AudioCaptureFrame metadata = {false, false, false, 3, 0};
        const int before = wake_reset_calls;
        for (int index = 0; index < 30; ++index) { cache_wake_audio(pcm.data(), pcm.size()); note_wake_boundary_audio(pcm.size(), metadata); metadata.continuous = true; metadata.sample_index += 320; }
        require(wake_reset_calls == before + 1 && !g_wake_boundary_reset && g_wake_prefix_count == 19200, "invalid_vad_disabled_normal_wake_detection");
        metadata.generation++; note_wake_boundary_audio(pcm.size(), metadata); require(wake_reset_calls == before + 2, "generation_change_kept_stale_model");
    });
    run_case("quiet_boundary_resets_model_once_and_never_crops_frozen_prefix", [] {
        prepare_manual(); feed(pattern(30000, 4)); const auto pcm = pattern(640, 5);
        AudioCaptureFrame metadata = {true, false, false, 5, 0}; const int before = wake_reset_calls;
        for (int index = 0; index < 40; ++index) { cache_wake_audio(pcm.data(), pcm.size()); note_wake_boundary_audio(pcm.size(), metadata); metadata.continuous = true; metadata.sample_index += 320; }
        require(wake_reset_calls == before + 2, "silence_reset_model_more_than_once");
        g_wake_clip_supported = false; snapshot_wake_prefix(); const auto count = g_wake_prefix_count; metadata.generation++;
        note_wake_boundary_audio(32000, metadata);
        require(g_wake_prefix_count == count && wake_reset_calls == before + 2, "frozen_prefix_was_trimmed");
    });

    std::cout << "{\"cases\":" << cases << ",\"failures\":" << failures << ",\"checks\":" << checks << ",\"actual_functions\":26,\"device_used\":false,\"firmware_built\":false}" << '\n';
    return failures ? 1 : 0;
}
