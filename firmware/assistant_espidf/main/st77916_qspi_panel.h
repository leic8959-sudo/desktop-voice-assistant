#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"

struct St77916QspiCommand {
    uint8_t code;
    const uint8_t *data;
    uint8_t length;
    uint16_t delay_ms;
};

// Borrowed static commands and IO; reviewed360x360 RGB565/QSPI, GPIO or NC software reset.
esp_err_t st77916_qspi_panel_create(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *config,
    const St77916QspiCommand *commands, size_t command_count, esp_lcd_panel_handle_t *handle);
