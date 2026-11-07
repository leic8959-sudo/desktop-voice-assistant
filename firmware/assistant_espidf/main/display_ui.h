#pragma once
#include <stdint.h>
#include "esp_err.h"

enum class UiState : uint8_t { Idle, Listening, Speaking, Thinking, Executing, Connecting, Alert, Error, Notification, Updating };
esp_err_t display_ui_init();
void display_ui_log_diagnostics();
void display_ui_state(UiState state, const char *detail = nullptr, bool media = false);
bool display_ui_notification(const char *title, const char *due_at);
void display_ui_audio_level(uint8_t level);
void display_ui_nav_icon(uint8_t icon);
void display_ui_pairing(const char *code);
struct ScreenCardData {
    char card_id[81];
    char kind[25];
    char title[181];
    char detail[541];
    char direction[17];
    char status[25];
    char reference_id[81];
    char reference_revision[65];
    char notification_lease[201];
    int32_t distance_m;
    int32_t duration_s;
    int32_t remaining_s;
    uint32_t ttl_s;
};
const char *display_ui_screen_card(const ScreenCardData &card);
void display_ui_screen_link(bool connected);
void display_ui_reminder_revision(const char *reference_id, const char *revision);
void display_ui_screen_action_result(const char *reference_id, bool accepted);
esp_err_t display_ui_apply_preferences(uint8_t brightness, uint8_t night_brightness, bool night_mode_enabled, uint32_t screen_off_seconds);
