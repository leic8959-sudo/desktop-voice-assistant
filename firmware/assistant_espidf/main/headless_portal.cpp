#include "headless_portal.h"

#include <cstring>
#include <cstdio>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "headless_portal_page.h"
#include "headless_ui.h"
#include "lwip/sockets.h"

namespace {
constexpr const char *TAG = "headless_portal";
constexpr int64_t kWindowUs = 600LL * 1000000;
SemaphoreHandle_t g_control;
httpd_handle_t g_server;
TaskHandle_t g_lifetime_task;
portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
char g_session[33];
int64_t g_deadline;
bool g_configure;
bool g_stop_failure_reported;

bool hotspot_request(httpd_req_t *request) {
    sockaddr_in local = {};
    socklen_t length = sizeof(local);
    esp_netif_ip_info_t info = {};
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap || esp_netif_get_ip_info(ap, &info) != ESP_OK) return false;
    if (getsockname(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr *>(&local), &length) != 0) return false;
    bool active;
    portENTER_CRITICAL(&g_state_lock);
    active = g_session[0] && esp_timer_get_time() < g_deadline;
    portEXIT_CRITICAL(&g_state_lock);
    return active && local.sin_family == AF_INET && local.sin_addr.s_addr == info.ip.addr;
}

esp_err_t json_response(httpd_req_t *request, cJSON *body) {
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
    char *text = cJSON_PrintUnformatted(body); cJSON_Delete(body);
    if (!text) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "内存不足");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    const esp_err_t result = httpd_resp_sendstr(request, text); cJSON_free(text); return result;
}

esp_err_t page(httpd_req_t *request) {
    if (!hotspot_request(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "请连接设备热点");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    return httpd_resp_sendstr(request, HEADLESS_PORTAL_PAGE);
}

esp_err_t session(httpd_req_t *request) {
    if (!hotspot_request(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "请连接设备热点");
    char token[33]; bool configure;
    portENTER_CRITICAL(&g_state_lock);
    std::memcpy(token, g_session, sizeof(token)); configure = g_configure;
    portEXIT_CRITICAL(&g_state_lock);
    cJSON *body = cJSON_CreateObject();
    if (!body) return json_response(request, nullptr);
    cJSON_AddStringToObject(body, "session", token); cJSON_AddBoolToObject(body, "configure", configure);
#if defined(ASSISTANT_BOARD_SPOT_C5)
    cJSON_AddStringToObject(body, "portal_gesture", "绑定后松开按键，再长按BOOT键3秒开启手机页面；KEY键按住说话，语音唤醒使用“你好小智”。");
#elif defined(ASSISTANT_BOARD_ATOM_M12_BASE)
    cJSON_AddStringToObject(body, "portal_gesture", "本机没有应用按键；重启后连接助手-配置热点，10分钟内可查看设备页面。语音请使用唤醒词。");
#elif defined(ASSISTANT_BOARD_EDA_BEAR)
    cJSON_AddStringToObject(body, "portal_gesture", "绑定后长按机身按钮3秒重新开启查看窗口；语音请使用唤醒词。");
#elif defined(ASSISTANT_BOARD_DF_AI_CAM)
    cJSON_AddStringToObject(body, "portal_gesture", "绑定后长按BOOT键3秒重新开启查看窗口；语音请使用唤醒词。");
#elif defined(ASSISTANT_BOARD_S31_CORE)
    cJSON_AddStringToObject(body, "portal_gesture", "绑定后长按BOOT键3秒重新开启；语音请使用唤醒词。");
#elif defined(ASSISTANT_BOARD_ATOM_ECHOS3R)
    cJSON_AddStringToObject(body, "portal_gesture", "绑定后长按机身按钮3秒重新开启；语音请使用唤醒词。");
#else
    cJSON_AddStringToObject(body, "portal_gesture", "绑定后同时按住两枚音量键3秒重新开启。");
#endif
    return json_response(request, body);
}

esp_err_t status(httpd_req_t *request) {
    if (!headless_portal_authorized(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "查看窗口已关闭");
    HeadlessUiSnapshot snapshot = {};
    if (!headless_ui_snapshot(&snapshot)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "状态暂不可用");
    cJSON *body = cJSON_CreateObject();
    if (!body) return json_response(request, nullptr);
    cJSON_AddNumberToObject(body, "state", static_cast<unsigned>(snapshot.state));
    cJSON_AddStringToObject(body, "detail", snapshot.detail);
    cJSON_AddStringToObject(body, "pairing_code", snapshot.pairing_code);
    cJSON_AddNumberToObject(body, "pairing_seconds", snapshot.pairing_seconds);
    cJSON_AddBoolToObject(body, "linked", snapshot.linked);
    cJSON_AddBoolToObject(body, "action_pending", snapshot.action_pending);
    if (snapshot.has_card) {
        cJSON *card = cJSON_AddObjectToObject(body, "card");
        if (!card) { cJSON_Delete(body); return json_response(request, nullptr); }
        cJSON_AddStringToObject(card, "card_id", snapshot.card.card_id);
        cJSON_AddStringToObject(card, "kind", snapshot.card.kind);
        cJSON_AddStringToObject(card, "title", snapshot.card.title);
        cJSON_AddStringToObject(card, "detail", snapshot.card.detail);
        cJSON_AddStringToObject(card, "status", snapshot.card.status);
        cJSON_AddStringToObject(card, "reference_revision", snapshot.card.reference_revision);
        cJSON_AddNumberToObject(card, "generation", snapshot.card_generation);
        cJSON_AddNumberToObject(card, "remaining_s", snapshot.card.remaining_s);
    } else cJSON_AddNullToObject(body, "card");
    return json_response(request, body);
}

esp_err_t action(httpd_req_t *request) {
    if (!headless_portal_authorized(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "查看窗口已关闭");
    if (request->content_len <= 0 || request->content_len > 512) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "请求长度无效");
    char text[513] = {}; int received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, text + received, request->content_len - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "读取请求失败");
        received += count;
    }
    cJSON *body = cJSON_ParseWithLength(text, received);
    const cJSON *id = body ? cJSON_GetObjectItem(body, "card_id") : nullptr;
    const cJSON *revision = body ? cJSON_GetObjectItem(body, "revision") : nullptr;
    const cJSON *command = body ? cJSON_GetObjectItem(body, "action") : nullptr;
    const cJSON *generation = body ? cJSON_GetObjectItem(body, "generation") : nullptr;
    const bool valid = cJSON_IsString(id) && std::strlen(id->valuestring) <= 80 && cJSON_IsString(revision) && std::strlen(revision->valuestring) <= 64 && cJSON_IsString(command) && std::strlen(command->valuestring) <= 16 && cJSON_IsNumber(generation) && generation->valuedouble >= 0 && generation->valuedouble <= 4294967295.0 && generation->valuedouble == static_cast<uint32_t>(generation->valuedouble);
    const bool accepted = valid && headless_ui_action(id->valuestring, revision->valuestring, static_cast<uint32_t>(generation->valuedouble), command->valuestring);
    cJSON_Delete(body);
    if (!accepted) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "卡片已变化、离线或操作未被接受");
    cJSON *reply = cJSON_CreateObject();
    if (reply) cJSON_AddBoolToObject(reply, "accepted", true);
    return json_response(request, reply);
}

void lifetime_task(void *) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (xSemaphoreTake(g_control, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        bool expired;
        portENTER_CRITICAL(&g_state_lock);
        expired = g_server && esp_timer_get_time() >= g_deadline;
        if (expired) { g_session[0] = '\0'; g_deadline = 0; }
        portEXIT_CRITICAL(&g_state_lock);
        if (expired) {
            const esp_err_t stop = httpd_stop(g_server);
            if (stop == ESP_OK) {
                g_server = nullptr;
                g_stop_failure_reported = false;
                wifi_mode_t mode;
                const esp_err_t query = esp_wifi_get_mode(&mode);
                const esp_err_t result = query != ESP_OK ? query : mode == WIFI_MODE_APSTA ? esp_wifi_set_mode(WIFI_MODE_STA) : esp_wifi_stop();
                if (result != ESP_OK) ESP_LOGE(TAG, "hotspot shutdown failed: %s", esp_err_to_name(result));
                else ESP_LOGI(TAG, "phone portal window closed");
            } else if (!g_stop_failure_reported) {
                g_stop_failure_reported = true;
                ESP_LOGE(TAG, "phone portal stop failed: %s", esp_err_to_name(stop));
            }
        }
        xSemaphoreGive(g_control);
    }
}
}

bool headless_portal_authorized(httpd_req_t *request) {
    if (!hotspot_request(request) || httpd_req_get_hdr_value_len(request, "X-Device-Session") != 32) return false;
    char provided[33];
    if (httpd_req_get_hdr_value_str(request, "X-Device-Session", provided, sizeof(provided)) != ESP_OK) return false;
    bool matches;
    portENTER_CRITICAL(&g_state_lock);
    matches = g_session[0] && std::strcmp(provided, g_session) == 0 && esp_timer_get_time() < g_deadline;
    portEXIT_CRITICAL(&g_state_lock);
    return matches;
}

esp_err_t headless_portal_start(esp_err_t (*configure_handler)(httpd_req_t *)) {
    if (!g_control) g_control = xSemaphoreCreateMutex();
    if (!g_control) return ESP_ERR_NO_MEM;
    if (xSemaphoreTake(g_control, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (g_server) {
        bool active;
        portENTER_CRITICAL(&g_state_lock);
        active = g_session[0] != '\0';
        if (active) g_deadline = esp_timer_get_time() + kWindowUs;
        portEXIT_CRITICAL(&g_state_lock);
        xSemaphoreGive(g_control); return active ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    uint8_t random[16]; esp_fill_random(random, sizeof(random)); char token[33];
    for (size_t index = 0; index < sizeof(random); ++index) std::snprintf(token + index * 2, 3, "%02x", random[index]);
    portENTER_CRITICAL(&g_state_lock);
    std::memcpy(g_session, token, sizeof(token)); g_deadline = esp_timer_get_time() + kWindowUs; g_configure = configure_handler != nullptr;
    portEXIT_CRITICAL(&g_state_lock);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); config.stack_size = 8192; config.recv_wait_timeout = 5; config.send_wait_timeout = 5;
    esp_err_t result = httpd_start(&g_server, &config);
    const httpd_uri_t endpoints[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page, .user_ctx = nullptr },
        { .uri = "/device/session", .method = HTTP_GET, .handler = session, .user_ctx = nullptr },
        { .uri = "/device/status", .method = HTTP_GET, .handler = status, .user_ctx = nullptr },
        { .uri = "/device/action", .method = HTTP_POST, .handler = action, .user_ctx = nullptr },
        { .uri = "/configure", .method = HTTP_POST, .handler = configure_handler, .user_ctx = nullptr },
    };
    for (const auto &endpoint : endpoints) {
        if (result != ESP_OK) break;
        if (!endpoint.handler) continue;
        result = httpd_register_uri_handler(g_server, &endpoint);
    }
    if (result == ESP_OK && !g_lifetime_task && xTaskCreate(lifetime_task, "portal_window", 3072, nullptr, 2, &g_lifetime_task) != pdPASS) result = ESP_ERR_NO_MEM;
    if (result != ESP_OK) {
        portENTER_CRITICAL(&g_state_lock); g_session[0] = '\0'; g_deadline = 0; portEXIT_CRITICAL(&g_state_lock);
        if (g_server) {
            const esp_err_t stop = httpd_stop(g_server);
            if (stop == ESP_OK) g_server = nullptr;
            else ESP_LOGE(TAG, "failed portal cleanup: %s", esp_err_to_name(stop));
        }
    } else ESP_LOGI(TAG, "phone portal ready;AP interface only;600second window");
    xSemaphoreGive(g_control); return result;
}
