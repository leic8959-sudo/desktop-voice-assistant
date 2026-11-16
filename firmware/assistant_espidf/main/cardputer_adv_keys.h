#pragma once

#include "driver/i2c_master.h"
#include "manual_input.h"

esp_err_t cardputer_adv_keys_init(i2c_master_bus_handle_t bus);
ManualInputKey cardputer_adv_read_keys();
