#pragma once
#include "device_config.h"
#include "esp_err.h"

esp_err_t control_protocol_start(const DeviceConfig *config);
void control_protocol_listen_start();
void control_protocol_listen_stop();
void control_protocol_abort();
void control_protocol_send_device_state(const char *state, const char *detail = nullptr);

bool control_protocol_queue_screen_action(const char *action, const char *reference_id, const char *revision);
