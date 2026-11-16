#pragma once

#include "esp_err.h"

esp_err_t atom_s3r_backlight_init();
esp_err_t atom_s3r_set_brightness(int percent);
esp_err_t atom_s3r_detect_panel(bool *gc9107);
