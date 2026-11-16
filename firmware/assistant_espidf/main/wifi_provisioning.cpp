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
#if defined(ASSISTANT_BOARD_HEADLESS)
#include "headless_portal.h"
#endif

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

#if !defined(ASSISTANT_BOARD_HEADLESS)
static esp_err_t configure_page(httpd_req_t *req) {
#if defined(ASSISTANT_BOARD_ML307)
    static const char page[] = "<!doctype html><meta charset=utf-8><title>伊迪斯设备配置</title><h2>连接伊迪斯</h2><p>请确认SIM卡和蜂窝网络已准备好，再保存服务地址。联网后绑定码将在设备屏幕显示。</p><form id=f><label>服务地址 <input name=server required placeholder=wss://bridge/device/v1/></label><details><summary>兼容旧设备令牌</summary><input name=token type=password minlength=24></details><button>保存并连接</button></form><p id=m></p><script>f.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(f));let r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});m.textContent=await r.text()}</script>";
#elif defined(ASSISTANT_BOARD_RNDIS)
    static const char page[] = "<!doctype html><meta charset=utf-8><title>伊迪斯设备配置</title><h2>连接伊迪斯</h2><p>连接USB RNDIS网络设备后，保存服务地址。绑定码将在设备屏幕显示。</p><form id=f><label>服务端 WS URL <input name=server required placeholder=ws://bridge/device/v1/></label><details><summary>兼容旧设备令牌</summary><input name=token type=password minlength=24></details><button>保存并连接</button></form><p id=m></p><script>f.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(f));let r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});m.textContent=await r.text()}</script>";
#elif defined(ASSISTANT_BOARD_NT26)
    static const char page[] = "<!doctype html><meta charset=utf-8><title>伊迪斯设备配置</title><h2>连接伊迪斯</h2><p>请确认NT26模块和SIM卡已准备好，再保存服务地址。联网后绑定码将在设备屏幕显示。</p><form id=f><label>服务地址 <input name=server required placeholder=ws://bridge/device/v1/></label><details><summary>兼容旧设备令牌</summary><input name=token type=password minlength=24></details><button>保存并连接</button></form><p id=m></p><script>f.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(f));let r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});m.textContent=await r.text()}</script>";
#else
    static const char page[] = "<!doctype html><meta charset=utf-8><title>助手配网</title><h2>连接伊迪斯</h2><p>配置网络后，设备屏幕会显示配对码，请在后台添加设备。</p><form id=f><label>Wi-Fi <input name=ssid required></label><br><label>密码 <input name=password type=password></label><br><label>服务端 WS URL <input name=server required placeholder=ws://bridge/device/v1/></label><details><summary>兼容旧设备令牌</summary><input name=token type=password minlength=24></details><button>保存并连接</button></form><p id=m></p><script>f.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(f));let r=await fetch('/configure',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});m.textContent=await r.text()}</script>";
#endif
    httpd_resp_set_type(req, "text/html; charset=utf-8"); return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}
#endif

static esp_err_t configure_post(httpd_req_t *req) {
#if defined(ASSISTANT_BOARD_HEADLESS)
    if (!g_config || !headless_portal_authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "请在设备热点的配置窗口中操作");
#endif
    if (req->content_len <= 0 || req->content_len > 1024) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请求过大");
    char body[1025] = {}; int received = 0;
    while (received < req->content_len) { int n = httpd_req_recv(req, body + received, req->content_len - received); if (n <= 0) return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "读取请求失败"); received += n; }
    cJSON *json = cJSON_ParseWithLength(body, received); if (!json) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "需要 JSON");
    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid"); const cJSON *password = cJSON_GetObjectItem(json, "password"); const cJSON *server = cJSON_GetObjectItem(json, "server"); const cJSON *token = cJSON_GetObjectItem(json, "token");
#if defined(ASSISTANT_BOARD_RNDIS) || defined(ASSISTANT_BOARD_NT26) || defined(ASSISTANT_BOARD_ML307)
    if (!cJSON_IsString(server) || !server->valuestring[0]) { cJSON_Delete(json); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "server 必填"); }
#else
    if (!cJSON_IsString(ssid) || !ssid->valuestring[0] || !cJSON_IsString(server) || !server->valuestring[0]) { cJSON_Delete(json); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid/server 必填"); }
#endif
    if ((cJSON_IsString(ssid) && strlen(ssid->valuestring) > sizeof(g_config->wifi_ssid) - 1) || (cJSON_IsString(password) && strlen(password->valuestring) > sizeof(g_config->wifi_password) - 1) || strlen(server->valuestring) > sizeof(g_config->ws_url) - 1 || (cJSON_IsString(token) && strlen(token->valuestring) > sizeof(g_config->device_token) - 1)) { cJSON_Delete(json); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "字段过长或 token 缺失"); }
#if !defined(ASSISTANT_BOARD_RNDIS) && !defined(ASSISTANT_BOARD_NT26) && !defined(ASSISTANT_BOARD_ML307)
    strlcpy(g_config->wifi_ssid, ssid->valuestring, sizeof(g_config->wifi_ssid));
    if (cJSON_IsString(password)) strlcpy(g_config->wifi_password, password->valuestring, sizeof(g_config->wifi_password));
#endif
    strlcpy(g_config->ws_url, server->valuestring, sizeof(g_config->ws_url));
    strlcpy(g_config->device_token, cJSON_IsString(token) ? token->valuestring : "", sizeof(g_config->device_token));
    cJSON_Delete(json);
    esp_err_t err = device_config_save(g_config);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "保存失败");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
#if defined(ASSISTANT_BOARD_HEADLESS)
    httpd_resp_sendstr(req, "已保存，设备将重启。请重新连接助手-配置热点，刷新本页读取绑定码。");
#elif defined(ASSISTANT_BOARD_RNDIS)
    httpd_resp_sendstr(req, "已保存，设备将重启连接USB RNDIS网络。绑定码将在屏幕显示。");
#elif defined(ASSISTANT_BOARD_NT26)
    httpd_resp_sendstr(req, "已保存，设备将重启连接NT26蜂窝网络。绑定码将在屏幕显示。");
#elif defined(ASSISTANT_BOARD_ML307)
    httpd_resp_sendstr(req, "已保存，设备将重启连接蜂窝网络。绑定码将在屏幕显示。");
#else
    httpd_resp_sendstr(req, "已保存，设备将重启连接 Wi-Fi");
#endif
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
#if defined(ASSISTANT_BOARD_HEADLESS)
    return headless_portal_start(configure_post);
#else
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = nullptr;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &server_config), TAG, "http server");
    httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = configure_page, .user_ctx = nullptr};
    httpd_uri_t submit = {.uri = "/configure", .method = HTTP_POST, .handler = configure_post, .user_ctx = nullptr};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &page), TAG, "page");
    return httpd_register_uri_handler(server, &submit);
#endif
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
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "sta start");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "sta connect");
#if defined(ASSISTANT_BOARD_HEADLESS)
#if defined(ASSISTANT_BOARD_ATOM_M12_BASE)
    // CAM/M12 has no application key: a reboot opens the existing bounded
    // phone window. The portal still accepts requests only on the AP address.
    return provisioning_open_device_portal();
#else
    if (!device_config_ready(config)) return provisioning_open_device_portal();
#endif
#endif
    return ESP_OK;
}

#if defined(ASSISTANT_BOARD_HEADLESS)
esp_err_t provisioning_open_device_portal() {
    // Called at unbound startup or by a physical key chord. The existing STA
    // connection stays active while the phone uses the local AP interface.
    if (!g_ap_netif) g_ap_netif = esp_netif_create_default_wifi_ap();
    if (!g_ap_netif) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "phone portal APSTA");
    wifi_config_t ap = {};
    strlcpy(reinterpret_cast<char *>(ap.ap.ssid), "助手-配置", sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(reinterpret_cast<char *>(ap.ap.ssid));
    ap.ap.channel = 1; ap.ap.max_connection = 2; ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "phone portal AP config");
    return headless_portal_start(nullptr);
}
#endif
