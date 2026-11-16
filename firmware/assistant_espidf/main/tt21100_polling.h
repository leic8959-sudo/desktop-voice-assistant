#pragma once

#include "driver/i2c_master.h"

struct Tt21100Frame { bool pressed; uint16_t x, y; };

// Fixed ESP-BSP TT21100 raw reports. One consumer, borrowed persistent bus.
class Tt21100Polling {
public:
    esp_err_t Init(i2c_master_bus_handle_t bus);
    esp_err_t Read(Tt21100Frame *frame);
private:
    i2c_master_dev_handle_t device_ = nullptr;
    Tt21100Frame last_{};
    bool valid_ = false;
};
