#include "device_config.h"
#include "wifi_provisioning.h"
#include "display_ui.h"
#include "audio_io.h"
#include "control_protocol.h"
#include "storage.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "assistant";

static void log_startup_memory(const char *phase) {
    ESP_LOGI(TAG, "startup_memory phase=%s internal_free=%u internal_largest=%u psram_free=%u", phase,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

// 配对阶段不启用音频/工具协议，设备 nonce 仅用于领取本次会话凭据。
static cJSON *pairing_post(const char *url, const char *body, int *status) {
    esp_http_client_config_t options = {};
    options.url = url; options.timeout_ms = 10000;
    options.crt_bundle_attach = esp_crt_bundle_attach;
    options.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&options);
    if (!client) return nullptr;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    cJSON *result = nullptr;
    esp_err_t error = esp_http_client_open(client, strlen(body));
    if (error == ESP_OK) {
        int sent = 0;
        while (sent < static_cast<int>(strlen(body))) {
            int count = esp_http_client_write(client, body + sent, strlen(body) - sent);
            if (count <= 0) break;
            sent += count;
        }
        if (sent == static_cast<int>(strlen(body)) && esp_http_client_fetch_headers(client) >= 0) {
            *status = esp_http_client_get_status_code(client);
            char response[2048] = {}; int received = 0;
            while (received < static_cast<int>(sizeof(response) - 1)) {
                int count = esp_http_client_read(client, response + received, sizeof(response) - 1 - received);
                if (count <= 0) break;
                received += count;
            }
            if (esp_http_client_is_complete_data_received(client)) result = cJSON_ParseWithLength(response, received);
        }
    } else ESP_LOGW(TAG, "pairing_http_failed error=%s", esp_err_to_name(error));
    esp_http_client_close(client); esp_http_client_cleanup(client);
    return result;
}

static esp_err_t pair_device(DeviceConfig *config) {
    char base[224];
    const bool secure = strncmp(config->ws_url, "wss://", 6) == 0;
    const char *authority = config->ws_url + (secure ? 6 : 5);
    const char *slash = strchr(authority, '/');
    const size_t length = slash ? static_cast<size_t>(slash - authority) : strlen(authority);
    if (!length || length > 180 || memchr(authority, '@', length)) return ESP_ERR_INVALID_ARG;
    snprintf(base, sizeof(base), "%s://%.*s", secure ? "https" : "http", static_cast<int>(length), authority);
    uint8_t random[24]; esp_fill_random(random, sizeof(random));
    char nonce[49]; for (size_t index = 0; index < sizeof(random); ++index) snprintf(nonce + index * 2, 3, "%02x", random[index]);
    char body[256]; snprintf(body, sizeof(body), "{\"device_nonce\":\"%s\"}", nonce);
    char url[384]; snprintf(url, sizeof(url), "%s/api/devices/pairing/device-start", base);
    int status = 0;
    cJSON *session = pairing_post(url, body, &status);
    const cJSON *identifier = session ? cJSON_GetObjectItem(session, "pairing_id") : nullptr;
    const cJSON *code = session ? cJSON_GetObjectItem(session, "pairing_code") : nullptr;
    if (status != 201 || !cJSON_IsString(identifier) || !cJSON_IsString(code) || strlen(identifier->valuestring) > 64 || strlen(code->valuestring) != 6) { cJSON_Delete(session); return ESP_FAIL; }
    for (const char *digit = code->valuestring; *digit; ++digit) {
        if (!isdigit(static_cast<unsigned char>(*digit))) { cJSON_Delete(session); return ESP_FAIL; }
    }
    display_ui_pairing(code->valuestring);
    snprintf(url, sizeof(url), "%s/api/devices/pairing/%s/device-result", base, identifier->valuestring);
    snprintf(body, sizeof(body), "{\"code\":\"%s\",\"device_nonce\":\"%s\"}", code->valuestring, nonce);
    cJSON_Delete(session);
    const int64_t deadline = esp_timer_get_time() + 600LL * 1000000;
    while (esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(3000)); status = 0;
        cJSON *result = pairing_post(url, body, &status);
        const cJSON *token = result ? cJSON_GetObjectItem(result, "device_credential") : nullptr;
        if (status == 200 && cJSON_IsString(token) && strlen(token->valuestring) >= 24 && strlen(token->valuestring) < sizeof(config->device_token)) {
            strlcpy(config->device_token, token->valuestring, sizeof(config->device_token)); cJSON_Delete(result);
            esp_err_t saved = device_config_save(config);
            if (saved == ESP_OK) display_ui_state(UiState::Connecting, "Pairing complete");
            return saved;
        }
        cJSON_Delete(result);
        if (status == 403 || status == 429) return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

static void button_task(void *) {
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << GPIO_NUM_0, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&cfg);
    int previous = 1;
    int64_t last_display_report = 0;
    while (true) {
        const int64_t now = esp_timer_get_time();
        if (now - last_display_report >= 10000000) { last_display_report = now; display_ui_log_diagnostics(); }
        int current = gpio_get_level(GPIO_NUM_0);
        if (previous == 1 && current == 0) control_protocol_listen_start();
        if (previous == 0 && current == 1) control_protocol_listen_stop();
        previous = current;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

extern "C" void app_main() {
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) { ESP_LOGE(TAG, "NVS 初始化失败: %s；保留现有分区，不自动擦除", esp_err_to_name(nvs_err)); return; }
    // 通用音频/模型分配超过1KB优先PSRAM；内部RAM留给PHY定时器、DMA和任务控制块。
    heap_caps_malloc_extmem_enable(1024);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_prepare());
    DeviceConfig config;
    ESP_ERROR_CHECK(device_config_load(&config));
    log_startup_memory("before_display");
    ESP_ERROR_CHECK(display_ui_init());
    log_startup_memory("after_display");
    storage_mount();
    if (!device_config_network_ready(&config)) {
        display_ui_state(UiState::Alert, "请连接“助手-配置”并访问192.168.4.1");
        ESP_ERROR_CHECK(provisioning_start(&config));
        return;
    }
    ESP_ERROR_CHECK(wifi_start(&config));
    // PHY定时器和连接资源先于音频前端分配；配对阶段不占用音频资源。
    display_ui_state(UiState::Connecting, "Connecting Wi-Fi");
    bool online = false;
    for (int attempt = 0; attempt < 60; ++attempt) {
        esp_netif_ip_info_t info = {};
        esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (station && esp_netif_get_ip_info(station, &info) == ESP_OK && info.ip.addr) { online = true; break; }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!online) { display_ui_state(UiState::Error, "Wi-Fi timeout\nRestart to retry"); return; }
    log_startup_memory("wifi_ready");
    if (!device_config_ready(&config)) {
        esp_err_t paired = pair_device(&config);
        if (paired != ESP_OK) {
            ESP_LOGW(TAG, "pairing_failed error=%s", esp_err_to_name(paired));
            display_ui_state(UiState::Error, "Pairing failed / expired\nRestart to retry");
            return;
        }
    }
    esp_err_t audio = audio_io_init();
    log_startup_memory("after_audio");
    if (audio != ESP_OK) {
        ESP_LOGE(TAG, "audio_init_failed error=%s", esp_err_to_name(audio));
        display_ui_state(UiState::Error, "Audio initialization failed");
        return;
    }
    xTaskCreate(button_task, "button", 3072, nullptr, 5, nullptr);
    // Wi-Fi event readiness is handled by the websocket client's reconnect loop.
    ESP_ERROR_CHECK(control_protocol_start(&config));
    ESP_LOGI(TAG, "固件启动：控制通道=%s", config.ws_url);
}
