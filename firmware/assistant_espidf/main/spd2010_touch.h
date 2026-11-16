#pragma once

#include "esp_lcd_panel_io.h"

struct Spd2010Point { uint8_t id; uint16_t x, y; uint8_t weight; };
struct Spd2010Frame { uint8_t count; Spd2010Point points[10]; };

// Fixed2.1.0 polling protocol. Board finishes reset/INT GPIO configuration;
// borrowed IO uses address53/no command prefix and outlives this single owner.
class Spd2010Touch {
public:
    esp_err_t Init(esp_lcd_panel_io_handle_t io);
    esp_err_t Read(Spd2010Frame *frame);
private:
    esp_err_t Command(uint8_t command, uint8_t value);
    esp_err_t ReadRegister(uint8_t low, uint8_t high, uint8_t *data, size_t size);
    esp_err_t Poll(Spd2010Frame *frame);
    esp_lcd_panel_io_handle_t io_ = nullptr;
    Spd2010Frame last_{};
    bool valid_ = false;
};
