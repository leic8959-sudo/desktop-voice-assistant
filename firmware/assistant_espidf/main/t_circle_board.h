#pragma once

#include "esp_err.h"
#include "esp_codec_dev.h"
#include "esp_lvgl_port.h"
#include "digital_audio.h"

#define BSP_CAPS_TOUCH 1
#define BSP_CAPS_SDCARD 0

// Local board adapter, not an external BSP or an upstream application port.
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
