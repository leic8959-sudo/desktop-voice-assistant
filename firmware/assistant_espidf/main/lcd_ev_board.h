#pragma once

#include "driver/i2c_master.h"
#include "esp_io_expander.h"

esp_err_t lcd_ev_board_init(i2c_master_bus_handle_t bus);
esp_io_expander_handle_t lcd_ev_get_expander();
esp_err_t lcd_ev_set_amplifier(bool enabled);
