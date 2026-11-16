#include "storage.h"
#include <stdio.h>
#include <errno.h>
#include "esp_log.h"
#include "board_profile.h"
#include "esp_check.h"

static const char *TAG = "storage";
#if BSP_CAPS_SDCARD
static bool g_mounted;
#endif

esp_err_t storage_mount() {
#if BSP_CAPS_SDCARD
    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) { ESP_LOGW(TAG, "TF 卡未挂载: %s", esp_err_to_name(err)); return err; }
    g_mounted = true;
    ESP_LOGI(TAG, "TF 卡已挂载，记忆仅为服务端快照缓存");
    return ESP_OK;
#else
    ESP_LOGI(TAG, "当前固件未启用SD缓存；记忆权威副本仍在服务端");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void storage_write_memory_snapshot(const char *json) {
#if BSP_CAPS_SDCARD
    if (!g_mounted || !json) return;
    FILE *file = fopen(BSP_SD_MOUNT_POINT "/memory_snapshot.json.tmp", "wb"); if (!file) return;
    fputs(json, file); fclose(file);
    const char *current = BSP_SD_MOUNT_POINT "/memory_snapshot.json";
    const char *backup = BSP_SD_MOUNT_POINT "/memory_snapshot.json.bak";
    remove(backup);
    if (rename(current, backup) != 0 && errno != ENOENT) { ESP_LOGE(TAG, "备份记忆快照失败"); remove(BSP_SD_MOUNT_POINT "/memory_snapshot.json.tmp"); return; }
    if (rename(BSP_SD_MOUNT_POINT "/memory_snapshot.json.tmp", current) != 0) {
        ESP_LOGE(TAG, "替换记忆快照失败，尝试恢复旧文件"); rename(backup, current); return;
    }
    remove(backup);
#else
    (void)json;
#endif
}
