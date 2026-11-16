#pragma once

#include "driver/i2c_master.h"
#include "manual_input.h"

esp_err_t stopwatch_board_init(i2c_master_bus_handle_t bus);
esp_err_t stopwatch_set_amplifier(bool enabled);
ManualInputKey stopwatch_read_keys();
