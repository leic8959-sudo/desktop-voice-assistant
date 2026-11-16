#include "device_websocket_client.h"

#include <cstring>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#if defined(ASSISTANT_BOARD_ML307)
#include <utility>
#include "device_network.h"
#endif

namespace {
constexpr const char *kTag = "device_ws";
}

DeviceWebSocketClient::~DeviceWebSocketClient() { (void)Reset(); }

esp_err_t DeviceWebSocketClient::Init(const char *uri, const char *headers) {
    if (client_) return ESP_ERR_INVALID_STATE;
    if (!uri || !uri[0]) return ESP_ERR_INVALID_ARG;
#if defined(ASSISTANT_BOARD_ML307)
    auto owner = device_network_ml307_owner();
    if (!owner) { ESP_LOGE(kTag, "cellular WebSocket owner missing"); return ESP_ERR_INVALID_STATE; }
    Ml307WebSocketClient::Options options;
    options.uri = uri;
    options.headers = headers;
    auto created = Ml307WebSocketClient::Create(std::move(owner), options);
    if (!created) {
        ESP_LOGE(kTag, "cellular WebSocket init failed error=%s native=%d", created.error().Name(), created.error().native);
        return created.error().native == ESP_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    cellular_ = std::move(*created);
    client_ = cellular_->handle();
#else
    esp_websocket_client_config_t options = {};
    options.uri = uri;
    options.headers = headers;
    options.buffer_size = 4096;
    options.task_stack = 8192;
    options.reconnect_timeout_ms = 5000;
    options.network_timeout_ms = 10000;
    options.ping_interval_sec = 10;
    options.pingpong_timeout_sec = 15;
    if (std::strncmp(uri, "wss://", 6) == 0) options.crt_bundle_attach = esp_crt_bundle_attach;
    client_ = esp_websocket_client_init(&options);
    if (!client_) { ESP_LOGE(kTag, "WebSocket init failed"); return ESP_FAIL; }
#endif
    return ESP_OK;
}

esp_err_t DeviceWebSocketClient::Reset() {
    if (!client_) return ESP_OK;
#if defined(ASSISTANT_BOARD_ML307)
    cellular_.reset(); // SDK client, external WS child, then authenticated parent.
    client_ = nullptr;
    return ESP_OK; // Local cleanup, not confirmed peer/modem closure.
#else
    const esp_err_t result = esp_websocket_client_destroy(client_);
    if (result != ESP_OK) ESP_LOGE(kTag, "WebSocket destroy failed error=%s", esp_err_to_name(result));
    if (result == ESP_OK) client_ = nullptr;
    return result;
#endif
}
