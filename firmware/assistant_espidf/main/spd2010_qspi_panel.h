#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// Fixed SPD2010 412-square/QSPI/RGB565, NC software reset, gap0; borrowed IO.
esp_err_t spd2010_qspi_panel_create(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t *handle);
