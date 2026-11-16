#pragma once
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "manual_input.h"
#include "sensair_audio.h"

// The source treats CST816D as a touch key, not a mapped screen pointer.
#define BSP_CAPS_TOUCH 0
#define BSP_CAPS_SDCARD 0
esp_err_t sensair_board_init();
ManualInputKey sensair_read_keys();
esp_err_t bsp_i2c_init();
i2c_master_bus_handle_t bsp_i2c_get_handle();
lv_display_t *bsp_display_start();
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock();
esp_err_t bsp_display_brightness_set(int percent);
