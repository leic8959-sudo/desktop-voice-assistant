#include "device_network.h"

#if defined(ASSISTANT_BOARD_ML307)
#include <mutex>
#include <utility>
#include "esp_log.h"
namespace {
std::mutex g_owner_lock;
std::shared_ptr<Ml307ConnectionOwner> g_owner;
}

esp_err_t device_network_register_ml307_owner(std::shared_ptr<Ml307ConnectionOwner> owner) {
    if (!owner) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(g_owner_lock);
    if (g_owner) return ESP_ERR_INVALID_STATE;
    g_owner = std::move(owner);
    return ESP_OK;
}

std::shared_ptr<Ml307ConnectionOwner> device_network_ml307_owner() {
    std::lock_guard<std::mutex> lock(g_owner_lock);
    return g_owner;
}
#elif defined(ASSISTANT_BOARD_RNDIS)
#include "rndis_network.h"
#elif defined(ASSISTANT_BOARD_NT26)
#include "nt26_network.h"
#else
#include "esp_netif.h"
#include "wifi_provisioning.h"
#endif

esp_err_t device_network_start(const DeviceConfig *config) {
    if (!device_config_network_ready(config)) return ESP_ERR_INVALID_ARG;
#if defined(ASSISTANT_BOARD_ML307)
    auto owner = device_network_ml307_owner();
    if (!owner) { ESP_LOGE("device_network", "ML307 owner missing; startup stopped"); return ESP_ERR_INVALID_STATE; }
    auto started = owner->Start(60000);
    if (!started) {
        ESP_LOGE("device_network", "ML307 startup failed error=%s native=%d", started.error().Name(), started.error().native);
        return started.error().code == NetworkErrc::Timeout ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }
    return ESP_OK;
#elif defined(ASSISTANT_BOARD_RNDIS)
    return rndis_network_start();
#elif defined(ASSISTANT_BOARD_NT26)
    return nt26_network_start();
#else
    return wifi_start(config);
#endif
}
bool device_network_has_address() {
#if defined(ASSISTANT_BOARD_ML307)
    auto owner = device_network_ml307_owner();
    return owner && owner->NetworkReady();
#elif defined(ASSISTANT_BOARD_RNDIS)
    return rndis_network_has_address();
#elif defined(ASSISTANT_BOARD_NT26)
    return nt26_network_has_address();
#else
    esp_netif_ip_info_t info = {};
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return station && esp_netif_get_ip_info(station, &info) == ESP_OK && info.ip.addr;
#endif
}
const char *device_network_name() {
#if defined(ASSISTANT_BOARD_ML307)
    return "ML307 cellular";
#elif defined(ASSISTANT_BOARD_RNDIS)
    return "USB RNDIS";
#elif defined(ASSISTANT_BOARD_NT26)
    return "NT26 cellular";
#else
    return "Wi-Fi";
#endif
}
