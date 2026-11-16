// Panel command data adapted from XiaoZhi v2.5.2 at49b4380b3faa0ceadd28a3e78aaf0f311c46c57e.
// Copyright (c)2025 Shenzhen Xinzhi Future Technology Co., Ltd.
// Copyright (c)2025 Project Contributors.
// MIT notice: third_party/xiaozhi_spotpear/LICENSE.
#pragma once
#include "esp_lcd_panel_io.h"

inline esp_err_t spotpear_panel_tune(esp_lcd_panel_io_handle_t io) {
#if defined(ASSISTANT_BOARD_SPOTPEAR_BOX)
    static const uint8_t timing62[] = {0x18, 0x0d, 0x71, 0xed, 0x70, 0x70, 0x18, 0x0f, 0x71, 0xef, 0x70, 0x70};
    static const uint8_t timing63[] = {0x18, 0x11, 0x71, 0xf1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xf3, 0x70, 0x70};
    static const uint8_t voltage[] = {0x1f};
    esp_err_t result = esp_lcd_panel_io_tx_param(io, 0x62, timing62, sizeof(timing62));
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(io, 0x63, timing63, sizeof(timing63));
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(io, 0xc3, voltage, sizeof(voltage));
    if (result == ESP_OK) result = esp_lcd_panel_io_tx_param(io, 0xc4, voltage, sizeof(voltage));
    return result;
#else
    static const uint8_t voltage[] = {0x38};
    return esp_lcd_panel_io_tx_param(io, 0xbb, voltage, sizeof(voltage));
#endif
}
