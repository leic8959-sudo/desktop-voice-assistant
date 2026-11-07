# 小燕子本地 OTA 0.3.8 发布记录

日期：2026-11-04（Asia/Shanghai）。牛牛明确要求“发布ota”，本轮执行固件构建、离线检查、本地 OTA 工件更新、网关短暂切换及设备升级。仅当前已绑定设备，无公开互联网固件发布、无 Git 上传、无串口擦除或全量烧录。

## 最终状态

- 目标设备：`desk-1770bd2934c31b2f`。
- 发布前：在线、固件0.3.7。
- 2026-11-04 14:00:44（北京时间）：网关记录0.3.7握手，推送0.3.8 OTA；设备从局域网下载 `/api/ota/firmware`，HTTP 200。
- 14:01:01：设备再次握手，报告固件 **0.3.8**；后续 `/api/devices` 复查仍在线。
- `/api/screen/state`：设备声明 `supports_cards=true`，收到卡片 `expired` 回执，证明新协议已运行；不等同真人视觉 / 触控 / 提示音体验验收。
- `runtime/pairing-sessions.json` 升级前后 SHA-256 相同；绑定记录未变。不输出其值或设备凭据。
- 网关从 PID63164 切换到 PID4128，保留原启动参数 `gateway.py --config config.local.json` 和 Python 解释器；新后端桌面工具 / 配置接口已加载。未输出或手动读取配置秘密。
- 新网关本轮日志中 `Traceback` 数量0。旧版虚假默认离线设备条目不再返回，实际在线设备仍为原绑定设备。

## 固件工件

| 项目 | 值 |
| --- | --- |
| 源码版本 | 0.3.8 |
| ESP-IDF | 已安装官方5.5.6 |
| 官方构建产物 | `build_official/assistant_espidf.bin` |
| 固定发布副本 | `firmware/releases/0.3.8/assistant_espidf.bin` 与 `manifest.json` |
| 当前 OTA 下载路径 | `firmware/assistant_espidf/build/assistant_espidf.bin` |
| 大小 | 2576928 字节 |
| SHA-256 | `b2b3a9ab2412766a143640d3408d9a76ff6147e7b65702cd443d7637269e5654` |
| OTA 应用分区 | 3,145,728字节，剩余约18% |

原 OTA 接口读取 build 目录，本轮将验证通过的官方构建应用文件经临时文件原子替换到该路径，不更换分区表、NVS、模型或存储文件。发布副本、当前下载工件与构建产物 SHA-256 一致。旧应用工件备份在 `runtime/ota-038-before/`；保留本地恢复材料，不宣称已演练完整回滚。

## 实际检查命令与结果

1. 设置当前子进程的 `IDF_PATH`、`IDF_TOOLS_PATH`、`IDF_PYTHON_ENV_PATH`，加载现有 `export.ps1`，执行：
   `python <已安装IDF>/tools/idf.py -C firmware/assistant_espidf -B <项目根>/build_official build`
   退出0，应用编译、链接、生成镜像及分区大小检查通过。第一次调用缺少已有 Python 虚拟环境路径，返回路径不存在；补正确的 `IDF_PYTHON_ENV_PATH` 后通过，未安装依赖。构建保留 CMake 和 LVGL 弃用 API 警告，无编译错误。
2. `python -m unittest tests.test_console_configuration tests.test_gateway_v1 tests.test_pairing_persistence tests.test_assistant_core tests.test_chat_pipeline -q`：**56项通过**，45.180秒。全部临时数据 / 模拟服务，未访问真实第三方。
3. `python -m unittest tests.test_desktop_ota -q`：**4项通过**，1.368秒。新增发布前检查覆盖计时创建重复请求、暂停/恢复/到期/重启恢复、保存失败保持旧状态、管理员权限、无设备时不假称上屏、提醒稍后单次变更。仅临时目录、隔离随机端口、假设备。
4. `node tests/console_configuration_ui.mjs`：通过；`node --check web/desktop.js`：退出0。
5. Python 读取固件 app_desc 魔数 / 版本 / 大小 / SHA，确认0.3.8且小于3MiB；发布和下载路径逐项哈希一致。
6. 现有本机管理员 Cookie 接口读取 `/api/devices`、`/api/screen/state`：同设备在线0.3.8、有卡片能力和回执。未创建真实用户计时器 / 提醒，未读邮箱 / 笔记 / 任务正文。
7. 网关日志确认真实设备 OTA GET200，然后0.3.8重连。额外管理员 Cookie 直接下载工件返回401：现有 OTA handler 仍要求 Bearer设备/管理员凭据，不接受仅 Cookie 的二次校验；本轮未为方便下载而放宽该边界，也未将其记录为设备升级失败。
8. `git status --short`：当前不是Git仓库。没有 add/commit/push 等 Git 写操作。

## 证据与边界

- `evidence/ota-038-build.log`、`ota-038-idf-environment.log`：构建记录。
- `evidence/ota-038-backend-checks.log`、`ota-038-desktop-checks.log`：离线检查结果；模拟异常堆栈与弃用警告不代表真实账号故障。
- `evidence/ota-038-artifact.json`、`ota-038-binding-check.json`、`ota-038-device-readback.json`：工件、绑定一致性和设备回读。
- `runtime/ota-038-gateway.stderr.log` / stdout：新网关运行日志；未把原始运行配置或秘密包含到发布工件。

本轮已实际完成设备OTA和新网关加载。屏幕卡片真实可见性、字体截断、触控按钮、离线计时精度、提示音与唤醒并发还需牛牛现场验收；没有因版本握手成功推断所有功能体验通过。高德、Google/Microsoft、家居等账号的真实权限和业务调用未在本次发布中验收。不做第三方授权、发消息、写内容、解绑或重新配对。
