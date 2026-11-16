#include "headless_ui.h"

#include <cstring>
#include <cstdio>
#include "board_power.h"
#include "control_protocol.h"
#if defined(ASSISTANT_BOARD_DOIT) || defined(ASSISTANT_BOARD_DF_AI_CAM)
#include "driver/gpio.h"
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "headless_ui";
struct Card {
    ScreenCardData data{};
    int64_t received_us = 0;
    uint32_t generation = 0;
    bool active = false, seen = false, cue_sent = false;
};
SemaphoreHandle_t g_lock;
Card g_reminder, g_timer;
UiState g_state = UiState::Idle;
char g_detail[192], g_code[7], g_pending_reference[81];
int64_t g_code_deadline, g_action_deadline;
bool g_linked, g_manual_ready, g_pending;

bool lock() { return g_lock && xSemaphoreTake(g_lock, pdMS_TO_TICKS(200)) == pdTRUE; }
void unlock() { xSemaphoreGive(g_lock); }

Card *visible_card() {
    if (g_reminder.active) return &g_reminder;
    return g_timer.active ? &g_timer : nullptr;
}

int32_t remaining(const Card &card) {
    const int64_t elapsed = std::strcmp(card.data.status, "running") == 0 ? (esp_timer_get_time() - card.received_us) / 1000000 : 0;
    const int64_t result = static_cast<int64_t>(card.data.remaining_s) - elapsed;
    return result > 0 ? static_cast<int32_t>(result) : 0;
}

void expire(Card &card, int64_t now) {
    if (!card.active) return;
    if (std::strcmp(card.data.kind, "reminder") == 0 && !card.cue_sent) {
        // A local alert sound may ask the user to open the portal. It is not
        // evidence that reminder text was presented or read on the phone.
        card.cue_sent = control_protocol_queue_screen_action("cue_reminder", "", "");
    }
    if (std::strcmp(card.data.kind, "timer") == 0 && std::strcmp(card.data.status, "running") == 0) {
        if (remaining(card) > 0) return;
        std::snprintf(card.data.status, sizeof(card.data.status), "finished");
        std::snprintf(card.data.detail, sizeof(card.data.detail), "计时结束，等待后台同步");
        card.data.remaining_s = 0; card.data.ttl_s = 30; card.received_us = now;
        ++card.generation; card.seen = false;
    }
    if (std::strcmp(card.data.status, "finished") == 0 && !card.cue_sent) {
        card.cue_sent = control_protocol_queue_screen_action("cue_timer", "", "");
    }
    if (std::strcmp(card.data.status, "paused") != 0 && now - card.received_us >= static_cast<int64_t>(card.data.ttl_s) * 1000000) {
        control_protocol_queue_screen_action("receipt", card.data.card_id, "expired");
        card.active = false;
    }
}

void status_task(void *) {
    while (true) {
        if (lock()) {
            const int64_t now = esp_timer_get_time();
            expire(g_reminder, now); expire(g_timer, now);
            if (g_code_deadline && now >= g_code_deadline) { g_code[0] = '\0'; g_code_deadline = 0; }
            if (g_pending && now >= g_action_deadline) {
                g_pending = false;
                std::snprintf(g_detail, sizeof(g_detail), "操作结果待核实，请到控制台查看");
            }
#if defined(ASSISTANT_BOARD_DOIT) || defined(ASSISTANT_BOARD_DF_AI_CAM)
            // Fast blink signals error/reminder; slow blink onboarding/network.
            // Each board below retains its own GPIO and polarity.
            const bool urgent = g_state == UiState::Error || g_state == UiState::Alert || g_reminder.active;
            const bool waiting = g_code[0] || !g_linked || !g_manual_ready;
            const bool speaking = g_state == UiState::Listening || g_state == UiState::Speaking;
            const int period_ms = urgent ? 250 : 1000;
            const bool on = speaking || ((urgent || waiting) && (now / (period_ms * 1000LL)) % 2 == 0);
#if defined(ASSISTANT_BOARD_DF_AI_CAM)
            const esp_err_t result = gpio_set_level(GPIO_NUM_3, on ? 1 : 0);
#else
            const esp_err_t result = gpio_set_level(GPIO_NUM_45, on ? 0 : 1);
#endif
            if (result != ESP_OK) ESP_LOGE(TAG, "LED update failed: %s", esp_err_to_name(result));
#endif
            unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
}

esp_err_t display_ui_init() {
    if (g_lock) return ESP_OK;
    const esp_err_t power = board_power_init();
    if (power != ESP_OK) return power;
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(status_task, "headless_ui", 3072, nullptr, 2, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "headless presentation ready;phone portal required for pairing/card visibility");
    return ESP_OK;
}
esp_err_t display_ui_enable_manual_input() {
    if (!lock()) return ESP_ERR_TIMEOUT;
    g_manual_ready = true; unlock(); return ESP_OK;
}
bool display_ui_handle_hardware_button(bool) { return false; }
bool display_ui_cancel_hardware_button() { return true; }
void display_ui_log_diagnostics() {
    if (!lock()) return;
    ESP_LOGI(TAG, "state=%u linked=%d card=%d pending=%d", static_cast<unsigned>(g_state), g_linked, visible_card() != nullptr, g_pending);
    unlock();
}
void display_ui_state(UiState state, const char *detail, bool) {
    if (!lock()) return;
    if (g_state == UiState::Updating && state != UiState::Updating && state != UiState::Error) { unlock(); return; }
    g_state = state;
    std::snprintf(g_detail, sizeof(g_detail), "%s", detail ? detail : "");
    if (g_code[0]) { g_code[0] = '\0'; g_code_deadline = 0; }
    unlock();
}
bool display_ui_notification(const char *title, const char *) {
    display_ui_state(UiState::Notification, title);
    // No visual delivery claim;phone seen acknowledgement is separate.
    return false;
}
void display_ui_audio_level(uint8_t) {}
void display_ui_nav_icon(uint8_t) {}
// [人工决策-2026-11-10 00:58:59] 牛牛选择无屏设备通过手机热点配网页读取六位绑定码。
void display_ui_pairing(const char *code) {
    if (!code || std::strlen(code) != 6 || !lock()) return;
    std::snprintf(g_code, sizeof(g_code), "%s", code);
    g_code_deadline = esp_timer_get_time() + 600LL * 1000000;
    g_state = UiState::Alert;
    std::snprintf(g_detail, sizeof(g_detail), "请在控制台输入手机页面显示的六位绑定码");
    unlock();
}
const char *display_ui_screen_card(const ScreenCardData &data) {
    // Match the current product contract:operation result cards are disabled;
    // only timers/reminders need a local headless presentation lifecycle.
    if (std::strcmp(data.kind, "timer") != 0 && std::strcmp(data.kind, "reminder") != 0) return "expired";
    if (!lock()) return "queued";
    Card &card = std::strcmp(data.kind, "reminder") == 0 ? g_reminder : g_timer;
    if (card.active && std::strcmp(card.data.card_id, data.card_id) != 0 && &card == &g_reminder) { unlock(); return "queued"; }
    const bool same = card.active && std::strcmp(card.data.card_id, data.card_id) == 0;
    const bool unchanged = same && std::strcmp(card.data.title, data.title) == 0 && std::strcmp(card.data.detail, data.detail) == 0 && std::strcmp(card.data.status, data.status) == 0 && std::strcmp(card.data.reference_revision, data.reference_revision) == 0 && std::strcmp(card.data.notification_lease, data.notification_lease) == 0 && card.data.remaining_s == data.remaining_s;
    char previous_revision[65]; std::snprintf(previous_revision, sizeof(previous_revision), "%s", card.data.reference_revision);
    const bool cue_sent = same && card.cue_sent;
    card.data = data; card.received_us = esp_timer_get_time(); card.active = std::strcmp(data.status, "cancelled") != 0;
    card.seen = unchanged && card.seen; card.cue_sent = cue_sent;
    if (!unchanged) ++card.generation;
    if (same && !card.data.reference_revision[0]) std::snprintf(card.data.reference_revision, sizeof(card.data.reference_revision), "%s", previous_revision);
    expire(card, card.received_us);
    const bool seen = card.active && card.seen;
    unlock(); return seen ? "shown" : "queued";
}
void display_ui_screen_link(bool connected) {
    if (!lock()) return;
    if (connected && !g_linked) {
        if (g_reminder.active) { g_reminder.seen = false; ++g_reminder.generation; }
        if (g_timer.active) { g_timer.seen = false; ++g_timer.generation; }
    }
    g_linked = connected; g_pending = false; unlock();
}
void display_ui_reminder_revision(const char *id, const char *revision) {
    if (!lock()) return;
    if (g_reminder.active && std::strcmp(g_reminder.data.reference_id, id) == 0) {
        if (std::strcmp(g_reminder.data.reference_revision, revision) != 0) { g_reminder.seen = false; ++g_reminder.generation; }
        std::snprintf(g_reminder.data.reference_revision, sizeof(g_reminder.data.reference_revision), "%s", revision);
    }
    unlock();
}
void display_ui_screen_action_result(const char *id, bool accepted) {
    if (!lock()) return;
    if (g_pending && std::strcmp(id, g_pending_reference) == 0) {
        g_pending = false;
        if (accepted && g_reminder.active && std::strcmp(id, g_reminder.data.reference_id) == 0) g_reminder.active = false;
        std::snprintf(g_detail, sizeof(g_detail), "%s", accepted ? "操作已确认" : "操作未完成，请核查控制台");
    }
    unlock();
}
esp_err_t display_ui_apply_preferences(uint8_t brightness, uint8_t night, bool, uint32_t screen_off) {
    if (brightness > 100 || night > 100 || screen_off > 86400) return ESP_ERR_INVALID_ARG;
    // There is no screen to apply these preferences to.
    return ESP_ERR_NOT_SUPPORTED;
}
bool headless_ui_snapshot(HeadlessUiSnapshot *snapshot) {
    if (!snapshot || !lock()) return false;
    *snapshot = {};
    snapshot->state = g_state; snapshot->linked = g_linked; snapshot->action_pending = g_pending;
    std::snprintf(snapshot->detail, sizeof(snapshot->detail), "%s", g_detail);
    const int64_t remaining_us = g_code_deadline - esp_timer_get_time();
    if (g_code[0] && remaining_us > 0) {
        std::snprintf(snapshot->pairing_code, sizeof(snapshot->pairing_code), "%s", g_code);
        snapshot->pairing_seconds = static_cast<uint32_t>((remaining_us + 999999) / 1000000);
    }
    Card *card = visible_card(); snapshot->has_card = card != nullptr;
    if (card) { snapshot->card = card->data; snapshot->card.remaining_s = remaining(*card); snapshot->card_generation = card->generation; }
    // Notification lease and device credentials are never exposed to the phone.
    snapshot->card.notification_lease[0] = '\0';
    unlock(); return true;
}
bool headless_ui_action(const char *card_id, const char *revision, uint32_t generation, const char *action) {
    if (!card_id || !revision || !action || !lock()) return false;
    Card *card = visible_card();
    if (!card || card->generation != generation || std::strcmp(card->data.card_id, card_id) != 0 || std::strcmp(card->data.reference_revision, revision) != 0) { unlock(); return false; }
    if (std::strcmp(action, "seen") == 0) {
        if (!g_linked) { unlock(); return false; }
        if (!card->seen) {
            if (!control_protocol_queue_screen_action("receipt", card_id, "shown")) { unlock(); return false; }
            card->seen = true;
        }
        if (card->data.notification_lease[0]) {
            if (!control_protocol_queue_screen_action("reminder_ack", card->data.reference_id, card->data.notification_lease)) { unlock(); return false; }
            card->data.notification_lease[0] = '\0';
        }
        unlock(); return true;
    }
    if (!card->seen || g_pending || g_state == UiState::Updating) { unlock(); return false; }
    if (std::strcmp(action, "close") == 0 && std::strcmp(card->data.kind, "reminder") == 0) { card->active = false; unlock(); return true; }
    const bool timer = std::strcmp(card->data.kind, "timer") == 0;
    const bool valid = timer ? std::strcmp(action, "cancel") == 0 || (std::strcmp(action, "pause") == 0 && std::strcmp(card->data.status, "running") == 0) || (std::strcmp(action, "resume") == 0 && std::strcmp(card->data.status, "paused") == 0)
        : std::strcmp(action, "snooze") == 0 && card->data.reference_revision[0];
    if (!valid || !g_linked || !control_protocol_queue_screen_action(action, card->data.reference_id, revision)) { unlock(); return false; }
    g_pending = true; g_action_deadline = esp_timer_get_time() + 8000000;
    std::snprintf(g_pending_reference, sizeof(g_pending_reference), "%s", card->data.reference_id);
    unlock(); return true;
}
