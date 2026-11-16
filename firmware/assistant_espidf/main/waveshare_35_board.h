#pragma once

#include "driver/i2c_master.h"

esp_err_t waveshare_35_board_init(i2c_master_bus_handle_t bus);
esp_err_t waveshare_35_set_amplifier(bool enabled);
