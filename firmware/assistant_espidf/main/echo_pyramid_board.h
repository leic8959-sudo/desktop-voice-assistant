#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t echo_pyramid_board_init(i2c_master_bus_handle_t bus);
esp_err_t echo_pyramid_set_amplifier(bool enabled);
esp_err_t echo_pyramid_audio_status();
