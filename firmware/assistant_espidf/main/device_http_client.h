#pragma once

#include "esp_http_client.h"
#if defined(ASSISTANT_BOARD_ML307)
#include <memory>
#include "ml307_clients.h"

#endif

// The SDK handle is borrowed; this object owns cleanup and external transport.
// Task owners must call Reset before vTaskDelete, which does not run C++ stack
// destructors. Destroy/reset only after all client users have joined.
class DeviceHttpClient {
public:
    DeviceHttpClient() = default;
    ~DeviceHttpClient();
    DeviceHttpClient(const DeviceHttpClient &) = delete;
    DeviceHttpClient &operator=(const DeviceHttpClient &) = delete;
    // Pairing/OTA requests use the same fixed certificate/redirect policy.
    esp_err_t Init(const char *url, int timeout_ms, int buffer_size = 0);
    esp_err_t Reset();
    esp_http_client_handle_t handle() const { return client_; }

private:
    esp_http_client_handle_t client_ = nullptr;
#if defined(ASSISTANT_BOARD_ML307)
    std::unique_ptr<Ml307HttpClient> cellular_;
#endif
};
