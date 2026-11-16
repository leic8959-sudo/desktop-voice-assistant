#pragma once

#include "driver/i2c_master.h"

// Reviewed CH32V0032.0.0 output/PWM subset. Init resets the WHOLE port;
// getters are deliberately absent: acknowledged writes are not pad readback.
// One board owner initializes before tasks and serializes runtime calls.
class Ch32Output {
public:
    esp_err_t Init(i2c_master_bus_handle_t bus);
    esp_err_t SetOutput(uint8_t mask, bool enabled);
    esp_err_t SetPwm(uint8_t value);
    bool failed() const { return failed_; }
private:
    esp_err_t Write(uint8_t address, uint8_t value);
    i2c_master_dev_handle_t device_ = nullptr;
    uint8_t output_ = 0;
    bool started_ = false, initialized_ = false, failed_ = false;
};
