#pragma once

#include "device_config.h"
#include "esp_err.h"
#if defined(ASSISTANT_BOARD_ML307)
#include <memory>
#include "ml307_transport.h"

// Register once before network start. HTTP and WS borrow this same owner;
// replacing it after ID exhaustion still requires a joined board lifecycle.
esp_err_t device_network_register_ml307_owner(std::shared_ptr<Ml307ConnectionOwner> owner);
std::shared_ptr<Ml307ConnectionOwner> device_network_ml307_owner();
#endif

esp_err_t device_network_start(const DeviceConfig *config);
bool device_network_has_address();
const char *device_network_name();
