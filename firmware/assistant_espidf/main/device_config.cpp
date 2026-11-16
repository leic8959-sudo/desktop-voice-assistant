#include "device_config.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_check.h"

static constexpr char kNamespace[] = "assistant";

static void read_string(nvs_handle_t handle, const char *key, char *output, size_t capacity) {
    size_t size = capacity;
    if (nvs_get_str(handle, key, output, &size) != ESP_OK) output[0] = '\0';
}

static esp_err_t import_existing_configuration(DeviceConfig *config) {
    nvs_handle_t handle;
    if (nvs_open("wifi", NVS_READONLY, &handle) != ESP_OK) return ESP_OK;
    read_string(handle, "ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    read_string(handle, "password", config->wifi_password, sizeof(config->wifi_password));
    nvs_close(handle);
    if (nvs_open("websocket", NVS_READONLY, &handle) != ESP_OK) return ESP_OK;
    read_string(handle, "url", config->ws_url, sizeof(config->ws_url));
    read_string(handle, "token", config->device_token, sizeof(config->device_token));
    nvs_close(handle);
    char *old_path = strstr(config->ws_url, "/xiaozhi/v1/");
    if (old_path && old_path[strlen("/xiaozhi/v1/")] == '\0') {
        strcpy(old_path, "/device/v1/");
    } else {
        config->ws_url[0] = '\0';
    }
    return device_config_ready(config) ? device_config_save(config) : ESP_OK;
}

esp_err_t device_config_load(DeviceConfig *config) {
    if (config == nullptr) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err == ESP_ERR_NVS_NOT_FOUND ? import_existing_configuration(config) : err;
    size_t size = sizeof(config->wifi_ssid); if (nvs_get_str(nvs, "wifi_ssid", config->wifi_ssid, &size) != ESP_OK) config->wifi_ssid[0] = '\0';
    size = sizeof(config->wifi_password); if (nvs_get_str(nvs, "wifi_password", config->wifi_password, &size) != ESP_OK) config->wifi_password[0] = '\0';
    size = sizeof(config->ws_url); if (nvs_get_str(nvs, "ws_url", config->ws_url, &size) != ESP_OK) config->ws_url[0] = '\0';
    size = sizeof(config->device_token); if (nvs_get_str(nvs, "device_token", config->device_token, &size) != ESP_OK) config->device_token[0] = '\0';
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t device_config_save(const DeviceConfig *config) {
    if (!device_config_network_ready(config) || (config->device_token[0] && strlen(config->device_token) < 24)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &nvs), "config", "open NVS");
    esp_err_t err = ESP_OK;
    err = nvs_set_str(nvs, "wifi_ssid", config->wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "wifi_password", config->wifi_password);
    if (err == ESP_OK) err = nvs_set_str(nvs, "ws_url", config->ws_url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "device_token", config->device_token);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

bool device_config_ready(const DeviceConfig *config) {
    return device_config_network_ready(config) && strlen(config->device_token) >= 24;
}

bool device_config_network_ready(const DeviceConfig *config) {
#if defined(ASSISTANT_BOARD_ML307)
    return config != nullptr && strnlen(config->ws_url, sizeof(config->ws_url)) < sizeof(config->ws_url) &&
        strncmp(config->ws_url, "wss://", 6) == 0;
#elif defined(ASSISTANT_BOARD_RNDIS) || defined(ASSISTANT_BOARD_NT26)
    // USB and UART Ethernet obtain their address independently of an SSID.
    return config != nullptr && strlen(config->ws_url) < sizeof(config->ws_url) &&
        (strncmp(config->ws_url, "ws://", 5) == 0 || strncmp(config->ws_url, "wss://", 6) == 0);
#else
    const size_t ssid_len = config ? strlen(config->wifi_ssid) : 0;
    return config != nullptr && ssid_len > 0 && ssid_len <= 32 && strlen(config->ws_url) < sizeof(config->ws_url) && (strncmp(config->ws_url, "ws://", 5) == 0 || strncmp(config->ws_url, "wss://", 6) == 0);
#endif
}
