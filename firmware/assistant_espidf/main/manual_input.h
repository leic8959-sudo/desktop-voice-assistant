#pragma once

#include <stdint.h>

// Some boards expose a GPIO, others expose discrete key codes over I2C.
// Invalid is a canceled press, never a normal release/confirmation.
enum class ManualInputKey : uint8_t { None, Listen, VolumeUp, VolumeDown, Invalid };
