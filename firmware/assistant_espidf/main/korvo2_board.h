#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "manual_input.h"

esp_err_t korvo2_board_init(i2c_master_bus_handle_t bus);
esp_err_t korvo2_set_backlight(int percent);
ManualInputKey korvo2_read_keys();
