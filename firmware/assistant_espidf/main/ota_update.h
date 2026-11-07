#pragma once
#include <stddef.h>
#include "esp_err.h"
// 从 bridge 下载固件写入备用 app 分区并切换启动；下载失败不影响当前运行。
// url 为局域网 HTTP 地址，token 用于下载鉴权，expected_sha256 为空串时跳过校验。
void ota_update_start(const char *url, const char *expected_sha256, const char *token);
bool ota_update_busy();
