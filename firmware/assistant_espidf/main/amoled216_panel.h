#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// Display-only candidate. Does not initialize PMIC, audio, touch or LVGL.
// Board initialization must provide the verified rails before calling this.
// One attempt per boot; failed partial objects remain alive, restart required.
esp_err_t amoled216_panel_create(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel);
esp_err_t amoled216_panel_set_brightness(int percent);
