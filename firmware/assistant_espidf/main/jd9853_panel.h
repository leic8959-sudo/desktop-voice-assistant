#pragma once

#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"

esp_err_t jd9853_panel_create(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *config, esp_lcd_panel_handle_t *panel);
