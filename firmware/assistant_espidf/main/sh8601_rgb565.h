#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"

struct Sh8601Command { uint8_t code; const uint8_t *data; uint8_t length; uint16_t delay_ms; };
struct Sh8601Geometry { uint16_t width, height; uint16_t x_gap, y_gap; };

// Borrow persistent32-bit QSPI IO and static command data. RGB565 only.
// Complete board setup/reset/rails and real hardware acceptance are separate.
// Reserve panel->user_data for the adapter. Join callers and drain raw IO
// before deletion; neither panel deletion nor the bridge owns raw IO.
esp_err_t sh8601_rgb565_create(esp_lcd_panel_io_handle_t raw_io, const esp_lcd_panel_dev_config_t *config,
    Sh8601Geometry geometry, const Sh8601Command *commands, size_t count, esp_lcd_panel_handle_t *panel);
