#include "wifi_provisioning.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "cJSON.h"

static const char *TAG = "wifi";
static DeviceConfig g_config_storage;
static DeviceConfig *g_config;
static esp_netif_t *g_ap_netif;
static bool g_wifi_initialized;

esp_err_t wifi_prepare() {
    if (g_wifi_initialized) return ESP_OK;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t result = esp_wifi_init(&init);
    if (result == ESP_OK) g_wifi_initialized = true;
    return result;
}

static void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi 断开，重新连接");
        esp_wifi_connect();
    }
}

static esp_err_t configure_page(httpd_req_t *req) {
    static const char page[] = "<!doctype html><meta charset=utf-8><title>助手配网</title><h2>连接小燕子</h2><p>配置网络后，设备屏幕会显示配对码，请在后台添加设备。</p><form id=f><label>Wi-Fi <input name=ssid required></label><br><label>密码 <input name=password type=password></label><br><label>服务端 WS URL <input name=server required placeholder=ws://bridge/device/v1/></label><details><summary>兼容旧设备令牌</summary><input name=token type=password minlength=24></details><button>保存并连接</button></form><p id=m></p><script>f.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(f));let r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});m.textContent=await r.text()}</script>";
    httpd_resp_set_type(req, "text/html; charset=utf-8"); return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t configure_post(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > 1024) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请求过大");
    char body[1025] = {}; int received = 0;
    while (received < req->content_len) { int n = httpd_req_recv(req, body + received, req->content_len - received); if (n <= 0) return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "读取请求失败"); received += n; }
    cJSON *json = cJSON_ParseWithLength(body, received); if (!json) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "需要 JSON");
    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid"); const cJSON *password = cJSON_GetObjectItem(json, "password"); const cJSON *server = cJSON_GetObjectItem(json, "server"); const cJSON *token = cJSON_GetObjectItem(json, "token");
    if (!cJSON_IsString(ssid) || !ssid->valuestring[0] || !cJSON_IsString(server) || !server->valuestring[0]) { cJSON_Delete(json); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid/server 必填"); }
    if (strlen(ssid->valuestring) > sizeof(g_config->wifi_ssid) - 1 || (cJSON_IsString(password) && strlen(password->valuestring) > sizeof(g_config->wifi_password) - 1) || strlen(server->valuestring) > sizeof(g_config->ws_url) - 1 || (cJSON_IsString(token) && strlen(token->valuestring) > sizeof(g_config->device_token) - 1)) { cJSON_Delete(json); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "字段过长或 token 缺失"); }
    strlcpy(g_config->wifi_ssid, ssid->valuestring, sizeof(g_config->wifi_ssid));
    if (cJSON_IsString(password)) strlcpy(g_config->wifi_password, password->valuestring, sizeof(g_config->wifi_password));
    strlcpy(g_config->ws_url, server->valuestring, sizeof(g_config->ws_url));
    strlcpy(g_config->device_token, cJSON_IsString(token) ? token->valuestring : "", sizeof(g_config->device_token));
    cJSON_Delete(json);
    esp_err_t err = device_config_save(g_config);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "已保存，设备将重启连接 Wi-Fi");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

esp_err_t provisioning_start(DeviceConfig *config) {
    g_config_storage = *config;
    g_config = &g_config_storage;
    ESP_RETURN_ON_ERROR(wifi_prepare(), TAG, "wifi init");
    g_ap_netif = esp_netif_create_default_wifi_ap();
    if (!g_ap_netif) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "ap mode");
    wifi_config_t ap = {};
    strlcpy(reinterpret_cast<char *>(ap.ap.ssid), "助手-配置", sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(reinterpret_cast<char *>(ap.ap.ssid));
    ap.ap.channel = 1; ap.ap.max_connection = 2; ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "ap start");
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &server_config), TAG, "http server");
    httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = configure_page, .user_ctx = nullptr};
    httpd_uri_t submit = {.uri = "/configure", .method = HTTP_POST, .handler = configure_post, .user_ctx = nullptr};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &page), TAG, "page");
    return httpd_register_uri_handler(server, &submit);
}

esp_err_t wifi_start(const DeviceConfig *config) {
    if (!device_config_network_ready(config)) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(wifi_prepare(), TAG, "wifi init");
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr), TAG, "wifi events");
    wifi_config_t station = {};
    strlcpy(reinterpret_cast<char *>(station.sta.ssid), config->wifi_ssid, sizeof(station.sta.ssid));
    strlcpy(reinterpret_cast<char *>(station.sta.password), config->wifi_password, sizeof(station.sta.password));
    station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "sta mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station), TAG, "sta config");
    return esp_wifi_start() == ESP_OK ? esp_wifi_connect() : ESP_FAIL;
}
