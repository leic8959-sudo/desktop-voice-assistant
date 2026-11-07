# 小燕子OTA 0.3.11：聆听点云稳定

日期：2026-11-04，Asia/Shanghai。牛牛明确要求“发布ota”。本次使用已有ESP-IDF环境编译当前固件，通过原有本地/局域网OTA通道完成升级，没有串口全量烧录、擦除NVS、重新配对、依赖安装或Git写操作。

## 实际结果

- 原设备`desk-1770bd2934c31b2f`发布前在线0.3.10。
- 北京时间 **20:47:30**：网关收到0.3.10握手并推送目标0.3.11，设备下载`/api/ota/firmware`返回200。
- **20:47:47**：设备握手报告 **0.3.11**；20:48再次读取仍在线，支持屏幕卡片。
- 设备偏好回执`audio=true`、`display=true`，说明设备报告相关应用接口成功；不等于已实测扬声器音量、屏幕亮度或夜间效果。电量仍为unknown，没有伪造电量读数。
- `config.local.json`与`runtime/pairing-sessions.json`发布前后SHA-256一致。绑定和凭据保持。
- 网关PID58388替换为29448，沿用原Python、工作目录和`gateway.py --config config.local.json`；新日志截至最终回读Traceback为0。
- 自动记忆保持开启且范围为偏好/长期事项；电脑媒体状态stopped、音源仍包含rain与white_noise；没有为了OTA执行真实音乐播放或账号调用。

## 发布内容

核心修复为`display_ui.cpp`的聆听点云：独立连续累加流场相位，避免状态速度系数乘累计时间造成切换跳变；等待用户说话时旋转、漂移、倾斜和扭曲明显减弱，降低横向拉伸，保留原非规则体积点团和微小流动。具体源码参数见`聆听点云稳定调整-20261104.md`。

本次编译也包含0.3.10发布后、此前已授权但尚未安装的固件偏好支持：对话/提醒音量与静音、音量通道切换、屏幕亮度/熄屏配置及应用回执，电量明确unknown。来源为当前`audio_io.cpp/.h`、`control_protocol.cpp`、`display_ui.cpp/.h`；没有回滚或丢弃这些已有改动。语音采集、唤醒模型与既有分区布局保持。音乐/播客/事件通知属于已加载的网关能力，不因这次OTA重新写入模型、曲库或账号。

## 构建和检查

- 既有ESP-IDF5.5.6，使用`IDF_PATH=C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`、`IDF_TOOLS_PATH=C:\Espressif\official-5.5.6`、现有Python虚拟环境加载`export.ps1`。
- 实际执行`python <IDF>/tools/idf.py -C firmware/assistant_espidf -B build_official build`：退出0，21步编译/链接及镜像、分区大小检查通过。保留既有LVGL弃用和CMake提示，没有编译错误；未自行安装依赖。
- `python -m unittest tests.test_desktop_ota tests.test_screen_feedback tests.test_pairing_persistence -q`：**18项通过**，7.847秒。使用临时目录和模拟服务；预期失败案例输出文件系统异常堆栈，不代表正式绑定保存失败。
- 发布前解析ESP32镜像头和app_desc魔数，确认嵌入版本0.3.11、目标esp32s3、镜像小于3MiB OTA分区。构建产物、固定发布副本和OTA下载工件三份SHA-256一致；认证HTTP下载返回200且大小/散列一致。
- 发布使用临时文件、fsync和原子替换；没有覆盖不同散列的同版本发布。原0.3.10发布工件与构建工件、源文件和受保护哈希保存在`runtime/ota-0311-before/`，未声称执行了实际回滚演练。
- 版本源文件为`firmware/assistant_espidf/main/version.txt`；本次由0.3.10更新为0.3.11，CMake已声明其configure依赖，构建描述及镜像嵌入版本均核对。

## 工件与证据

| 项目 | 值 |
| --- | --- |
| 版本 | 0.3.11 |
| 镜像大小 | 2583984字节 |
| SHA-256 | `38b5b0239adfc779ad729a4eb9ac2ea2eb5cc6685bde4772af3800abce9a5f90` |
| OTA应用分区 | 3145728字节，剩余561744字节，约18% |
| 构建产物 | `build_official/assistant_espidf.bin` |
| 固定发布副本 | `firmware/releases/0.3.11/assistant_espidf.bin`和`manifest.json` |
| 当前OTA下载工件 | `firmware/assistant_espidf/build/assistant_espidf.bin` |

证据：`evidence/ota-0311-idf-environment.log`、`ota-0311-build.log`、`ota-0311-backend-checks.log`、`ota-0311-artifact.json`、`ota-0311-device-readback.json`、`ota-0311-final-readback.json`、`ota-0311-binding-check.json`。网关日志为`runtime/ota-0311-gateway.stderr.log`及stdout。

设备版本握手证明新固件已运行，主机与HTTP散列证明发布工件一致；没有用这些代替现场视觉验收。等待说话时点云观感、回答切换、长时间帧预算、音量/亮度和提示音仍需牛牛现场体验确认。没有外网公开发布、Git提交/推送或额外账号操作。
