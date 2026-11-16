#pragma once

#include "driver/i2c_master.h"

esp_err_t pca9557_box_init(i2c_master_bus_handle_t bus);
esp_err_t pca9557_box_set_panel_control(bool high);
esp_err_t pca9557_box_set_amplifier(bool enabled);
