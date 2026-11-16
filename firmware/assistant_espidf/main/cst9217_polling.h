#pragma once

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

// Fixed Waveshare CST9217 legacy report, not a CST9220 protocol detector.
class Cst9217Polling {
public:
    esp_err_t Init(i2c_master_bus_handle_t bus, gpio_num_t reset, gpio_num_t interrupt);
    // A valid zero-contact/status0 release clears pressed. Errors cancel it.
    esp_err_t Read(bool *pressed, uint16_t *x, uint16_t *y);
private:
    esp_err_t Reset();
    esp_err_t ReadRegister(uint16_t reg, uint8_t *data, size_t length);
    i2c_master_dev_handle_t device_ = nullptr;
    gpio_num_t reset_ = GPIO_NUM_NC;
    bool attempted_ = false, ready_ = false;
};
