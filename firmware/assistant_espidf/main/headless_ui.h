#pragma once
#include "display_ui.h"

struct HeadlessUiSnapshot {
    UiState state;
    char detail[192];
    char pairing_code[7];
    uint32_t pairing_seconds;
    bool linked, has_card, action_pending;
    uint32_t card_generation;
    ScreenCardData card;
};

bool headless_ui_snapshot(HeadlessUiSnapshot *snapshot);
bool headless_ui_action(const char *card_id, const char *revision, uint32_t generation, const char *action);
