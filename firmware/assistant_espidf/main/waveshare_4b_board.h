#pragma once

#include "driver/i2c_master.h"
#include "esp_io_expander.h"

esp_err_t waveshare_4b_board_init(i2c_master_bus_handle_t bus);
esp_io_expander_handle_t waveshare_4b_get_expander();
esp_err_t waveshare_4b_set_amplifier(bool enabled);
