#pragma once

#include "esp_err.h"
#include "manual_input.h"

esp_err_t freenove_keys_init();
ManualInputKey freenove_keys_read();
