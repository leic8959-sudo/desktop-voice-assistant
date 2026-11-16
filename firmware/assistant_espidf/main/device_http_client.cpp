#include "device_http_client.h"

#include <cstring>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#if defined(ASSISTANT_BOARD_ML307)
#include "device_network.h"
#include <utility>
#endif

namespace {
constexpr const char *kTag = "device_http";
}

DeviceHttpClient::~DeviceHttpClient() { (void)Reset(); }

esp_err_t DeviceHttpClient::Init(const char *url, int timeout_ms, int buffer_size) {
    if (client_) return ESP_ERR_INVALID_STATE;
    if (!url || !url[0] || timeout_ms < 1 || timeout_ms > 60000 || buffer_size < 0) return ESP_ERR_INVALID_ARG;
#if defined(ASSISTANT_BOARD_ML307)
    if (buffer_size && buffer_size != 4096) return ESP_ERR_NOT_SUPPORTED;
    auto owner = device_network_ml307_owner();
    if (!owner) { ESP_LOGE(kTag, "cellular HTTP owner missing"); return ESP_ERR_INVALID_STATE; }
    auto created = Ml307HttpClient::Create(std::move(owner), url, timeout_ms);
    if (!created) {
        ESP_LOGE(kTag, "cellular HTTP init failed error=%s native=%d", created.error().Name(), created.error().native);
        return created.error().native == ESP_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    cellular_ = std::move(*created);
    client_ = cellular_->handle();
#else
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = timeout_ms;
    config.buffer_size = buffer_size;
    config.disable_auto_redirect = true;
    if (std::strncmp(url, "https://", 8) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    client_ = esp_http_client_init(&config);
    if (!client_) { ESP_LOGE(kTag, "HTTP init failed"); return ESP_FAIL; }
#endif
    return ESP_OK;
}

esp_err_t DeviceHttpClient::Reset() {
    if (!client_) return ESP_OK;
#if defined(ASSISTANT_BOARD_ML307)
    cellular_.reset(); // Factory destroys client, then its external TLS parent.
    client_ = nullptr;
    return ESP_OK; // Local cleanup only; not confirmed modem/peer closure.
#else
    const esp_err_t result = esp_http_client_cleanup(client_);
    if (result != ESP_OK) ESP_LOGE(kTag, "HTTP cleanup failed error=%s", esp_err_to_name(result));
    if (result == ESP_OK) client_ = nullptr;
    return result;
#endif
}
