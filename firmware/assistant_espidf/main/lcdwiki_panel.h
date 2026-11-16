#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// Fixed ST77922/QSPI portrait320x480, RGB565 buffer, NC software reset, gap0.
esp_err_t lcdwiki_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *handle);
