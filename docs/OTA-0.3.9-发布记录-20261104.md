# 小燕子本地 OTA 0.3.9 发布记录

日期：2026-11-04（Asia/Shanghai）。牛牛在屏幕状态覆盖修复后明确要求“发布ota”。本轮已完成固件构建、离线检查、工件原子发布、网关切换和已绑定设备 OTA。

## 实际结果

- 目标设备 `已配对样机`；发布前在线0.3.8，发布后在线 **0.3.9**，后续复查仍在线。
- 2026-11-04 **14:40:53**（北京时间）：旧版本设备重连，网关推送0.3.9；局域网设备下载 `/api/ota/firmware` 返回HTTP200。
- **14:41:10**：设备握手报告0.3.9；随后 `/api/screen/state` 回读设备支持新卡片，并有 `shown` 绘制回执。
- `runtime/pairing-sessions.json` 发布前后SHA-256一致。未解绑、重新配对、改设备凭据，未擦除NVS / 模型 / 存储或写入分区表。
- 网关从PID4128切换到PID24172，沿用原Python和 `gateway.py --config config.local.json`。新后端统一屏幕反馈入口已加载，日志中本轮Traceback数量0。
- 此次为当前本地 / 局域网设备OTA；没有公开互联网发布、Git提交、上传或推送。

## 发布内容

0.3.9包含23个注册语音工具和29项注册连接器动作的统一处理中 / 结果 / 空结果 / 待确认 / 失败 / 未知状态反馈；修复Listening挡住卡片、卡片等待显示时提前过期、同卡片状态变化未重绘，以及连续工具覆盖结果。普通结果有固定容量3的后续暂存队列。补默认中文状态说明、停止回答提示、模型连接过程和未来OTA下载 / 校验 / 重启提示。具体边界见 `docs/屏幕状态覆盖审查与修复-20261104.md`。

当前这次升级的下载提示仍由升级前0.3.8负责；不能据此声称用户已看到新固件的下一次OTA进度画面。

## 工件

| 项目 | 值 |
| --- | --- |
| 版本 | 0.3.9 |
| 官方工具链 | 已安装ESP-IDF5.5.6 |
| 应用大小 | 2579696字节 |
| SHA-256 | `7a7f0a24274406dbf11ef9e254feca2ce21c20a13fbd266097523289f8c57ce1` |
| OTA应用分区 | 3MiB，剩余约18% |
| 构建产物 | `build_official/assistant_espidf.bin` |
| 发布副本 | `firmware/releases/0.3.9/assistant_espidf.bin` 与 `manifest.json` |
| OTA下载工件 | `firmware/assistant_espidf/build/assistant_espidf.bin` |
| 原0.3.8备份 | `runtime/ota-039-before/published-038.bin`、`official-038.bin` |

验证app_desc魔数、版本、尺寸后，经临时文件原子替换现有OTA工件。构建、固定发布副本和当前下载工件三份SHA-256完全一致。没有运行串口全量烧录。

## 实际检查命令与结果

- 使用既有 `IDF_PATH` / `IDF_TOOLS_PATH` / `IDF_PYTHON_ENV_PATH` 加载export.ps1，执行 `python <IDF>/tools/idf.py -C firmware/assistant_espidf -B <项目>/build_official build`：退出0，编译、链接、镜像生成与分区大小检查成功。保留LVGL旧API弃用警告，无编译错误。没有安装依赖。
- 最终执行：`python -m unittest tests.test_screen_feedback tests.test_desktop_ota tests.test_console_configuration tests.test_gateway_v1 tests.test_pairing_persistence tests.test_assistant_core tests.test_chat_pipeline tests.test_gmail_api -q`：**74项通过，60.183秒**。全部临时目录、隔离端口、模拟服务/设备；未读真实邮件、笔记、任务内容或调用第三方授权 / 发送。
- 发布前首次67项回归有1失败 / 2错误：新增connecting消息后，旧测试把任意custom消息当语音就绪，云端配置断言过早。`tests/test_gateway_v1.py` 的事件等待增加显式state筛选，相关就绪断言改为等待listening，保留原云端调用和音色断言；不为通过测试删除真实connecting反馈。
- 新增 `tests/test_screen_feedback.py`，覆盖HTTP200但业务failed/unknown、空结果、保存 / OAuth / 确认凭证区分、敏感字段不回显、匿名 / 轮询不抢屏、API和语音共用反馈、辅助屏幕失败不改业务结果。初次7项中一条测试夹具缺aliyun_model导致状态接口404，补模拟模型字段后与全部检查一起通过。
- `node tests/console_configuration_ui.mjs`：通过。`node --check web/desktop.js`、`node --check web/admin_console.js`：退出0。
- 固件三份哈希核对与app_desc检查：通过。真实网关日志确认OTA GET200→设备0.3.9重连。
- `/api/devices` 与 `/api/screen/state` 经既有本机管理员Cookie读取：原设备在线、0.3.9、支持卡片、有shown回执；不输出管理员或设备凭据。
- 两个本轮测试文件 `git diff --no-index --check`：无空白错误；`git status --short` 仍为非Git仓库，没有Git写操作。

## 证据与剩余验收

工件：`evidence/ota-039-artifact.json`。设备 / 绑定：`ota-039-device-readback.json`、`ota-039-binding-check.json`。构建：`ota-039-build.log`、`ota-039-idf-environment.log`。检查：`ota-039-final-checks.log`；首次失败记录仍保留在 `ota-039-backend-checks.log` 和 `ota-039-feedback-checks.log`。新网关日志：`runtime/ota-039-gateway.stderr.log` / stdout。

设备绘制回执证明协议已处理到绘制路径，仍不等同真人可读性或点击体验验收。长语音结束后的卡片可见时间、连续结果顺序、字体省略、触控和提示音并发仍需牛牛现场确认；真实高德、Google/Microsoft、家居账号权限和业务执行未在本次OTA中验收。没有用HTTP200替代这些结论，也没有用固件版本握手声称全部体验通过。
