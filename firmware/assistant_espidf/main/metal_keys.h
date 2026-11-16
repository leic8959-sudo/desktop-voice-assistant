#pragma once

#include "esp_err.h"
#include "manual_input.h"

esp_err_t metal_keys_init();
ManualInputKey metal_keys_read();
