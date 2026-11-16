#pragma once

#include "driver/i2c_master.h"
#include "manual_input.h"

esp_err_t atk_box3_board_init(i2c_master_bus_handle_t bus);
esp_err_t atk_box3_set_amplifier(bool enabled);
esp_err_t atk_box3_set_backlight(bool enabled);
ManualInputKey atk_box3_read_keys();
