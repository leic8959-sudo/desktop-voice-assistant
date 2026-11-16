#pragma once

#include "driver/i2c_master.h"
#include "manual_input.h"

esp_err_t atk_box2_board_init(i2c_master_bus_handle_t bus);
esp_err_t atk_box2_set_amplifier(bool enabled);
// Owns only EN_4G/P1.4 when explicitly requested. A checked control level is
// not evidence of VBAT discharge or a clean modem session. Call with modem
// traffic quiescent; ordinary BOX2 Wi-Fi startup never invokes this function.
esp_err_t atk_box2_set_modem_enable(bool enabled);
ManualInputKey atk_box2_read_keys();
