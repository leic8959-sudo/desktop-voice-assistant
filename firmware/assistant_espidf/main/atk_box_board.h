#pragma once

#include "driver/i2c_master.h"

enum class AtkBoxAudio { Uninitialized, Es8311, Digital };
esp_err_t atk_box_board_init(i2c_master_bus_handle_t bus);
AtkBoxAudio atk_box_audio();
