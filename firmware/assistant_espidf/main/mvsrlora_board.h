#pragma once

#include "driver/i2c_master.h"

esp_err_t mvsrlora_board_init();
esp_err_t mvsrlora_set_amplifier(bool enabled);
esp_err_t mvsrlora_touch_init(i2c_master_bus_handle_t bus);
esp_err_t mvsrlora_read_touch(bool *pressed, uint16_t *x, uint16_t *y);
