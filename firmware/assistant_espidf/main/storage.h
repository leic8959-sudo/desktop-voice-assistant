#pragma once
#include "esp_err.h"

esp_err_t storage_mount();
void storage_write_memory_snapshot(const char *json);
