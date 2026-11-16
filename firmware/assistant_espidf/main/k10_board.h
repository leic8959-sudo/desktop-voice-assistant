#pragma once

#include "driver/i2c_master.h"
#include "manual_input.h"

esp_err_t k10_board_init(i2c_master_bus_handle_t bus);
esp_err_t k10_set_backlight(bool enabled);
ManualInputKey k10_read_keys();
uint8_t k10_adc_address();
