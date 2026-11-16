# 蜂窝传输接入核对与SDK预检（2026-11-12）

本批修正缓存构建工具的SDK版本预检，并为ML307传输接入固定接口证据和依赖补丁草稿。没有新增“已适配”板型，没有替换现有控制协议或安装依赖。当前仍是117／188个官方变体有源码入口、71个待源码，全部本轮实机验收未完成。

## 已修正的预检行为

此前 `tools/prepare_cached_firmware_build.py --idf-path ...` 只检查SDK目录完整性，不核对所选板的SDK版本约束。NT26虽然在单板manifest中要求 `>=6.0.1,<6.2.0`，传入本机5.5.6时仍先走组件检查；准备好组件后也可能暂存一个必然无法配置的目标。

现在在任何目录创建、复制和组件暂存前，从该板既有 `idf_component.yml` 读取 `idf` 约束，从指定SDK的 `tools/cmake/version.cmake` 读取MAJOR／MINOR／PATCH，并复用已安装ESP-IDF组件工具的SimpleSpec／Version判断是否匹配。不另建一份板型SDK表，不执行SDK脚本或安装程序。无法解析版本或不匹配时直接失败；未传入可选 `--idf-path` 时仍保持原有仅暂存行为。

暂存记录增加 `installed_sdk_base_version` 和 `required_sdk_version`。这只检查SDK声明的基础版本，不证明Git提交、dirty状态、子模块、工具链或干净发行构建已经验收。

真实执行以下命令（Python均为本机既有IDF5.5 Python环境）：

```text
python -B tools/prepare_cached_firmware_build.py --profile bread-compact-nt26 --destination I:/xy_board_builds_20261109/nt26_sdk_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp32s3-korvo2-v3-rndis --destination I:/xy_board_builds_20261109/rndis_sdk_preflight_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6
python -B tools/prepare_cached_firmware_build.py --profile bread-compact-wifi --destination I:/xy_board_builds_20261109/sdk_preflight_oled32_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6
```

| 目标 | 退出码 | 观察结果 |
| --- | --- | --- |
| NT26 | 1 | `Installed SDK 5.5.6 does not satisfy bread-compact-nt26 requirement >=6.0.1,<6.2.0; nothing was staged`；未创建目标目录 |
| Korvo2 RNDIS | 1 | SDK约束匹配后继续准确报缺 `espressif/iot_usbh_rndis`；未创建目标目录 |
| 默认OLED32 Wi-Fi | 0 | SDK5.5.6满足 `>=5.5.6,<5.6.0`，成功暂存17个既有组件；基础版本和约束已写入输入记录；未构建、安装或访问设备 |

正向暂存的205个main文件集合、文件摘要、板型身份／manifest和17个组件来源记录另做一致性核对。没有启动新固件构建或测试套件；上一批OLED32／RLCD的实际构建证据保持独立。

## 当前链路与可复用接入点

逐项检查当前源码后，设备HTTP客户端出现在启动绑定和OTA下载；媒体PCM与语音、控制回执共用控制WebSocket。设备端没有独立的媒体HTTP下载实现。因此后续ML307媒体验收应覆盖当前WebSocket PCM与暂停／恢复／回执，而不是假设已有另一个媒体HTTP客户端。

本机固定SDK5.5.6和WebSocket组件1.8.0有两个可复用接口：

- HTTP的 `CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT` 默认关闭；启用后 `esp_http_client_config_t.transport` 可注入自定义TCP transport。
- WebSocket的 `esp_websocket_client_config_t.ext_transport` 可注入WebSocket transport；该句柄必须满足SDKWebSocket transport的帧／父transport契约，不能直接塞入原始TCP对象。

候选方向是在原始AT TCP之上实现有界的同步transport，再使用ESP端TLS、证书包／主机名校验、现有HTTP解析器和WebSocket帧实现。保留当前分片、二进制音频、ping／pong、发送返回值、重连和业务回执逻辑。HTTP／WebSocket客户端对外部transport的关闭与销毁所有权不同，必须分别管理；不能把本机接口存在当作适配已完成。

`control_protocol.cpp`中的自然插话与唤醒人工决策，以及当前连接配置／事件处理，本批均保持字节不变。后续接入仍须保存这些决策边界，不能以换网络为由改掉语音行为。

## 精确ML307依赖发现

参考包为 `78/esp-ml307 3.7.5`，对应提交 `506477b5af639f5dce9386e423e939091fe7fa11`。新归档20份接口／实现／SDK参考文件，源文件按已保存仓库tree的Git blob SHA1核对，再记录SHA256；原包和原始参考均不修改。

1. `Ml307Ssl::ConfigureSsl`写入 `AT+MSSLCFG="auth",0,0`，接口未接收现有ESP证书包或主机名验证设置。不能据此声称其SSL与当前WSS认证等价，也不能直接用它替代当前证书验证。
2. `Ml307Tcp`在 `MIPURC` 分支只要求参数数目≥3，`rtcp`分支随后访问参数3，存在三参数输入越界；还未验证参数3是有效偶数字符串再进行十六进制解码。
3. `FIFO_OVERFLOW`在UART事件任务的URC回调内同步调用Disconnect，后者发送AT并等待同一个事件任务解析响应，可能造成自等待超时。回调列表确实有 `urc_mutex_` 保护，不把“回调列表完全无锁”作为结论。
4. MIPSEND确认路径未在每次发送前清除旧确认位，等待确认固定使用连接超时；迟到确认、调用方超时、复用连接ID及参数含义仍需准确模块AT协议和完整生命周期设计。参数1是否为状态码本批没有模块手册证据，不按猜测修改。

为第2、3项生成了精确依赖补丁草稿：

`evidence/cellular-transport-readiness-20261112/patches/ml307_tcp_receive_guard.patch`

草稿检查参数3存在且为偶数长度的十六进制字符串；畸形帧或FIFO溢出只置错误／断开通知，要求连接所属任务负责关闭远端连接，不在URC回调内等待AT。候选文件、原始／候选／补丁摘要和未解决项在同目录。**补丁只在evidence供审阅，未安装、应用到依赖或编译；它不是完整网络适配。**其他参数类型、错误传播、线程安全、发送时限、TLS、缓冲上限和掉线恢复仍需实现与验证。

## 当前状态

8个待源码ML307变体继续保持inventory_only，NT26／RNDIS各自源码与构建状态保留，没有增加缓存构建通过数。SDK预检结果与传输发现已同步研究矩阵；没有把待实现功能从全量目标删除。缺少兼容SDK、精确组件与对应实物的目标，仍不能完成构建或实机验收。

本批没有依赖或SDK安装、固件构建、测试套件、Git写操作、刷机、OTA、上传、发布或对外消息。完整命令结果、来源摘要、保护文件核对和本批diff见 `evidence/cellular-transport-readiness-20261112/`。
