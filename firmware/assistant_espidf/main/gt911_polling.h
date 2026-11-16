#pragma once

#include <stdint.h>
#include "esp_lcd_panel_io.h"

// Protocol register facts reviewed against Espressif GT911 1.2.1. This is a
// board-owned polling path, not a drop-in factory for GPIO reset/IRQ/sleep.
struct Gt911Point { uint8_t track_id; uint16_t x, y, strength; };
struct Gt911Frame { uint8_t count; Gt911Point points[5]; };

class Gt911Polling {
public:
    // The board must select the address and finish reset before this call.
    // Borrowed panel IO must use16-bit commands and outlive this object.
    esp_err_t Init(esp_lcd_panel_io_handle_t io);
    // Init runs before LVGL starts; one LVGL consumer owns every Read call.
    // Healthy not-ready polls preserve the last acknowledged frame. Before
    // the first valid frame or after failure, not-ready remains an error.
    esp_err_t Read(Gt911Frame *frame);

private:
    esp_lcd_panel_io_handle_t io_ = nullptr;
    Gt911Frame last_{};
    bool valid_ = false;
};
