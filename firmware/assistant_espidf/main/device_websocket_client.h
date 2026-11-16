#pragma once

#include "esp_websocket_client.h"
#if defined(ASSISTANT_BOARD_ML307)
#include <memory>
#include "ml307_clients.h"
#endif

// One owner for the client's lifetime. SDK handle is borrowed by protocol
// senders; join every sender before Reset/destruction, outside event/UART tasks.
class DeviceWebSocketClient {
public:
    DeviceWebSocketClient() = default;
    ~DeviceWebSocketClient();
    DeviceWebSocketClient(const DeviceWebSocketClient &) = delete;
    DeviceWebSocketClient &operator=(const DeviceWebSocketClient &) = delete;
    esp_err_t Init(const char *uri, const char *headers);
    esp_err_t Reset();
    esp_websocket_client_handle_t handle() const { return client_; }

private:
    esp_websocket_client_handle_t client_ = nullptr;
#if defined(ASSISTANT_BOARD_ML307)
    std::unique_ptr<Ml307WebSocketClient> cellular_;
#endif
};
