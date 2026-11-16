#pragma once

#include "driver/i2c_master.h"

esp_err_t atk_xl9555_init(i2c_master_bus_handle_t bus);
esp_err_t atk_xl9555_set_backlight(bool enabled);
esp_err_t atk_xl9555_set_amplifier(bool enabled);
