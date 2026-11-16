#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

struct AxsQspiCommand { uint8_t code; const uint8_t *data; uint8_t length; uint16_t delay_ms; };

// Borrow static table and IO; native RGB565/QSPI axes, gap0, sequential row writes.
esp_err_t axs_qspi_panel_create(esp_lcd_panel_io_handle_t io, int width, int height,
    const AxsQspiCommand *commands, size_t command_count, esp_lcd_panel_handle_t *handle);
