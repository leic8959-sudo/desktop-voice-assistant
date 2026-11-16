#pragma once

#include "esp_lcd_panel_io.h"

struct I80PanelIoConfig {
    int read, command_data, write, select, data[8], width;
    uint32_t clock_hz;
    size_t queue_depth;
    bool command_data_idle_high;
};
esp_err_t create_i80_panel_io(const I80PanelIoConfig &config, esp_lcd_panel_io_handle_t *io);
