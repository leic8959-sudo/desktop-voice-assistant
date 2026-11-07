# 自有 ESP-IDF 固件 v1

2026-10-29实际进展：已安装官方 `espressif/idf:v5.5.2` Docker环境，自有0.1.0固件已编译通过，应用约1.79MB，模型291149字节。`tools/build_firmware.ps1` 使用Linux卷缓存并导出 `build/flasher_args.json` 与5个二进制产物。USB端口恢复后按 `tools/flash_native.py` 执行，实际烧录记录以 `evidence/native-build-result-20261029.json` 为准。以下早期“无SDK/未构建”描述是前一轮静态交付的历史边界，不代表当前安装状态；实机启动、音频及家居验收仍需单独记录。

## 范围

`firmware/assistant_espidf` 是面向微雪 ESP32-S3-Touch-AMOLED-1.8 V2 的 ESP-IDF 5.5 固件工程。显示/触控/AXP2101/TCA9554 通过 Waveshare BSP `bsp_display_start()` 初始化，网络使用 ESP-IDF WebSocket 客户端，配置使用 NVS，TF 卡使用 BSP FATFS/SDMMC。

## 设备协议 `/device/v1/`

设备保持一个控制 WebSocket，不在连接时开启云端会话。握手请求：

```json
{"type":"hello","version":1,"transport":"websocket","audio_params":{"format":"pcm_s16le","sample_rate":16000,"channels":1,"frame_duration":20},"features":{"custom_ui":true}}
```

HTTP 头包含 `Authorization: Bearer <device_token>`、`Protocol-Version: 1`。服务器 hello 应返回输出 `pcm_s16le`、24000 Hz、单声道。

设备行为：

- BOOT 按下发送 `{"type":"listen","state":"start","mode":"manual"}` 并推送 16 kHz PCM16；松开发送 `listen/stop`，由桥接提交输入并请求回答。本地唤醒走连续 server VAD 对话。
- 中断发送 `{"type":"abort"}`，立即停播、清空本地播放通道，再发送 `device_state: idle`。
- 状态变化发送 `device_state`（`idle`、`listening`、`speaking`），服务端 `custom.payload.state` 也会更新本地 LVGL 状态。
- 下行二进制帧按 24 kHz PCM16 写入 I2S；`tts_start/tts_stop` 管理屏幕状态。
- 收到 `notification` 后先显示标题和 `due_at`，再回 `notification_ack`（同时带 `id`、`lease_token`）；收到不等于已提醒。

## 配网和本地缓存

首次启动会创建开放 AP `助手-配置`，访问 `/` 获得 HTML 表单；表单以 `POST /configure` 发送 JSON（ssid、password、server、token），敏感字段不放在 URL。设备校验 WS/WSS URL、SSID 长度和至少 24 字节 token 后写入 NVS 并重启。正常启动只从 NVS 读取 Wi-Fi、服务端 URL 和设备 token。TF 卡挂载到 `/sdcard`，服务端快照写入 `memory_snapshot.json`；SD 是缓存，不是权威记忆副本。设备启动后发送 `{"type":"memory_request","request_id":"boot"}`，桥接服务可回：

```json
{"type":"memory_snapshot","request_id":"boot","items":[{"key":"偏好.称呼","value":"牛牛","updated_at":"2026-10-29T00:00:00Z"}]}
```

设备校验 `request_id/items` 并通过临时文件与备份替换缓存；FAT断电恢复仍需实测。桥接限制快照条目总字节约5KB，`partial=true` 表示仅缓存部分记忆，不把它当离线全功能。

## 已实现与限制

- 已实现：BSP 板卡显示/触控/电源初始化、LVGL 状态卡、NVS 配网、常驻控制 WS、上行/下行 PCM、按键打断恢复、通知 ACK、SD 快照写入接口。
- 音频复用 Waveshare `esp32_s3_touch_amoled_1_8` 2.0.3 BSP 的 I2C/I2S/PA 定义和 Espressif `esp_codec_dev`／`es8311_codec_new`。硬件保持24kHz，采集转换为16kHz上行；播放使用24kHz。没有本地AEC，播放期间只排空采集、不上传、不跑唤醒，因此是轮流交互与按键打断，不宣称自然双讲。
- TF 卡使用同一 BSP 的 `bsp_sdcard_mount()` 和 `BSP_SD_MOUNT_POINT`，引脚由官方 2.0.3 组件管理，代码不再猜测 `SDMMC_SLOT_CONFIG_DEFAULT()`。
- 已接入 ESP-SR 2.0.0 WakeNet `wn9_hilexin` 官方接口；空闲时以 24 kHz codec 采集后下采样为 16 kHz，按模型 chunk 喂入，检测后触发 `listen start`。模型资源由 ESP-SR managed component 提供，未在本机下载或构建，模型可用性必须在 IDF 环境验证。
- `CONFIG_SR_WN_WN9_HILEXIN=y` 与 `CONFIG_MODEL_IN_FLASH=y` 让官方 ESP-SR 2.0 `movemodel.py` 将 `wn9_hilexin` 打包到独立 `model` 分区（4 MB）；运行时通过 `esp_srmodel_init("model")`、`esp_srmodel_filter(ESP_WN_PREFIX, "hilexin")` 选择实际模型名，构建日志会报告模型尺寸。
- 状态卡包含双眼、嘴形和中文状态字幕；通过 `CONFIG_LV_FONT_SIMSUN_16_CJK` 使用 LVGL SimSun CJK 字体，字体是否被当前 LVGL 组件实际编译仍需 IDF 构建确认。

## 可验证范围

本机没有 ESP-IDF 5.5.5、CMake 或 Ninja，因此未执行 `idf.py build`、烧录或 COM3 操作。已做源码级核对：目标为 `esp32s3`，分区适配 16 MB Flash，依赖声明包含 Waveshare `esp32_s3_touch_amoled_1_8` 2.0.3、LVGL、`esp_lvgl_port`、CO5300 和 CST816S；协议字段、鉴权头、采样率、通知租约 ACK 与父桥接约定一致。官方 2.0.3 API 证据为仓库 `firmware/brookesia/components/bsp_extra/src/bsp_board_extra.c` 与 `examples/esp-idf/09_sdmmc`、`12_i2s_codec`，其编解码器和 SD 初始化已按同一接口接入。获得 ESP-IDF 环境后应先运行：

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

构建通过后仍需单独验证 ES8311 出声/录音、TF 引脚、触控坐标、Wi-Fi 配网和服务端 `/device/v1/` 实机交互；这些均未因源码存在而视为已验证。
