#include "ota_update.h"

#include "display_ui.h"
#include "esp_log.h"

void ota_update_start(const char *, const char *, const char *) {
    // No download, task, erase or boot-partition change on a single-app board.
    ESP_LOGW("ota", "update rejected: single-app partition has no alternate slot");
    display_ui_notification("此设备请通过数据线更新固件", nullptr);
}
bool ota_update_busy() { return false; }
