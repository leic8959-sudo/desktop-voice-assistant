#include "ota_update.h"
#include "display_ui.h"
#include <stdio.h>
#include <string.h>
#include <atomic>
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

static const char *TAG = "ota";
static std::atomic<bool> g_busy{false};
// 下载缓冲放在任务栈上：写 flash 冻结 cache 期间被访问的内存必须都在内部 RAM。
static constexpr size_t kReadChunk = 2048;
// 应用分区 3MB；超过视为服务端异常，防止坏清单写满分区。
static constexpr size_t kMaxFirmwareBytes = 3 * 1024 * 1024;

static void ota_fail(esp_ota_handle_t handle, const esp_partition_t *partition, const char *reason) {
    ESP_LOGE(TAG, "%s", reason);
    if (handle) (void)esp_ota_abort(handle);
    (void)partition;
    g_busy = false;
    display_ui_state(UiState::Error, "升级未完成，旧应用分区未被覆盖；请到后台核查");
    vTaskDelete(nullptr);
}

static void ota_task(void *argument) {
    auto *context = static_cast<char **>(argument);
    const char *url = context[0];
    const char *expected_sha256 = context[1];
    const char *token = context[2];

    char chunk[kReadChunk];  // 任务栈(内部 RAM)上，见 kReadChunk 注释
    mbedtls_sha256_context sha = {};
    unsigned char digest[32];
    esp_ota_handle_t handle = 0;
    display_ui_state(UiState::Updating, "准备下载新固件，请保持供电");
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition) { ota_fail(0, nullptr, "ota_no_update_partition"); return; }

    esp_http_client_config_t http_config = {};
    http_config.url = url;
    http_config.timeout_ms = 10000;
    http_config.buffer_size = 4096;
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) { ota_fail(0, partition, "ota_http_init_failed"); return; }
    esp_http_client_set_header(client, "Authorization", token);

    ESP_LOGI(TAG, "ota_download url=%s partition=%s", url, partition->label);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    if (esp_http_client_open(client, 0) != ESP_OK) { esp_http_client_cleanup(client); ota_fail(0, partition, "ota_http_open_failed"); return; }
    const int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGE(TAG, "ota_http_status=%d", esp_http_client_get_status_code(client));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ota_fail(0, partition, "ota_http_status");
        return;
    }
    if (content_length <= 0 || static_cast<size_t>(content_length) > kMaxFirmwareBytes) {
        ESP_LOGE(TAG, "ota_bad_length=%d", content_length);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ota_fail(0, partition, "ota_bad_length");
        return;
    }

    if (esp_ota_begin(partition, content_length, &handle) != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ota_fail(0, partition, "ota_begin_failed");
        return;
    }
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    size_t total = 0;
    int previous_tenth = -1;
    while (total < static_cast<size_t>(content_length)) {
        const int read_bytes = esp_http_client_read(client, chunk, sizeof(chunk));
        if (read_bytes < 0) { ESP_LOGE(TAG, "ota_read_error at=%u", static_cast<unsigned>(total)); break; }
        if (read_bytes == 0) break;
        if (esp_ota_write(handle, chunk, static_cast<size_t>(read_bytes)) != ESP_OK) {
            ESP_LOGE(TAG, "ota_write_error at=%u", static_cast<unsigned>(total));
            break;
        }
        mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char *>(chunk), static_cast<size_t>(read_bytes));
        total += static_cast<size_t>(read_bytes);
        const int tenth = static_cast<int>(total * 10 / content_length);
        if (tenth != previous_tenth) { char progress[96]; snprintf(progress, sizeof(progress), "正在下载 %d%%\n请保持供电", tenth * 10); display_ui_state(UiState::Updating, progress); previous_tenth = tenth; }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total != static_cast<size_t>(content_length)) {
        mbedtls_sha256_free(&sha);
        ota_fail(handle, partition, "ota_incomplete_download");
        return;
    }
    mbedtls_sha256_finish(&sha, digest);
    display_ui_state(UiState::Updating, "下载完成，正在校验与设置启动分区");
    mbedtls_sha256_free(&sha);
    char actual[65] = {};
    for (int index = 0; index < 32; ++index) snprintf(actual + index * 2, 3, "%02x", digest[index]);
    if (expected_sha256[0] && strncasecmp(actual, expected_sha256, 64) != 0) {
        ESP_LOGE(TAG, "ota_sha_mismatch expected=%s actual=%s", expected_sha256, actual);
        ota_fail(handle, partition, "ota_sha_mismatch");
        return;
    }
    if (esp_ota_end(handle) != ESP_OK) { ota_fail(handle, partition, "ota_end_failed"); return; }
    if (esp_ota_set_boot_partition(partition) != ESP_OK) { ota_fail(handle, partition, "ota_set_boot_failed"); return; }
    ESP_LOGI(TAG, "ota_ready bytes=%u sha256=%s restarting", static_cast<unsigned>(total), actual);
    display_ui_state(UiState::Updating, "固件已校验，即将重启并重新连接");
    // 新分区处于 PENDING_VERIFY，重启后连接 bridge 成功即调用 mark_app_valid。
    g_busy = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

void ota_update_start(const char *url, const char *expected_sha256, const char *token) {
    bool idle = false;
    if (!g_busy.compare_exchange_strong(idle, true)) { ESP_LOGW(TAG, "ota_skip busy"); return; }
    if (!url || !url[0] || !token || !token[0]) { g_busy = false; ESP_LOGW(TAG, "ota_bad_args"); return; }
    static char context_url[256];
    static char context_sha[65];
    static char context_token[96];
    snprintf(context_url, sizeof(context_url), "%s", url);
    snprintf(context_sha, sizeof(context_sha), "%s", expected_sha256 ? expected_sha256 : "");
    snprintf(context_token, sizeof(context_token), "%s", token);
    static char *context[3] = {context_url, context_sha, context_token};
    // 栈必须在内部 RAM：esp_ota_write 冻结 cache 期间 PSRAM 栈不可访问（实测断言崩溃）。
    if (xTaskCreateWithCaps(ota_task, "ota_update", 8192, context, 4, nullptr, MALLOC_CAP_INTERNAL) != pdPASS) {
        g_busy = false;
        ESP_LOGW(TAG, "ota_task_create_failed");
    }
}

bool ota_update_busy() { return g_busy.load(); }
