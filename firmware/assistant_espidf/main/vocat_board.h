#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"

esp_err_t vocat_board_init(i2c_master_bus_handle_t bus);
bool vocat_board_ready();
gpio_num_t vocat_lcd_reset_pin();
bool vocat_lcd_reset_active_high();
esp_err_t vocat_set_amplifier(bool enabled);
