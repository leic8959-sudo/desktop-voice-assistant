#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// Fixed Taiji360x360 RGB565/QSPI, GPIO47 active-low reset; IO remains borrowed.
esp_err_t taiji_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *handle);
