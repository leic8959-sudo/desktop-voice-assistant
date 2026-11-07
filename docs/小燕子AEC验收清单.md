# 小燕子 AEC 验收清单

日期：2026-10-29（Asia/Shanghai）  
对象：Waveshare ESP32-S3 Touch AMOLED 1.8 V2、ES8311 单麦克风、ESP-SR 2.0 AEC

## 先给结论

这块板的 ES8311 可以同时开 I2S TX/RX，但“全双工”只描述输入输出通道能力，不等于存在扬声器到麦克风的硬件回采。板级定义只有一个单声道 `din` 和一个 `dout`；当前公开日志的 AFE 配置是 `total 1 channels(1 microphone, 0 playback)`。因此不能用 codec 初始化成功、播放无缺帧或 AEC 对象创建成功来宣布自然插话通过。

播放PCM可以作为合法的AEC数字参考，并不要求必须增加硬件回采。当前实现把播放PCM经过24kHz到16kHz重采样后送入ESP-SR `aec_process`；是否有效取决于实际播放时序、延迟和声学路径。仅有对象初始化和参考计数不能证明回声已消除。验收结论必须把下面三层分开。

## 三层验收

### 1. 软件初始化层（可由日志通过）

- ESP-SR AEC 创建成功，采样率为 16 kHz，帧长为头文件声明的 32 ms；`aec_stats enabled=1`。
- AFE 使用 M-only 作为单麦输入，AEC 的参考通道不要伪装成 AFE 的硬件 playback 通道。
- 每 10 秒或每轮会话记录 `mic_frames`、`reference_frames`、`output_frames`、`reference_underflows`、`capture_overflows`，并记录 `reference=software_pcm24` 等来源。
- 通过条件：初始化成功、计数持续增长、没有任务崩溃。此层不代表回声减量，也不代表真人插话。

### 2. 算法回声减量层（数字参考，只能记为软件证据）

- 播放 PCM 必须和送入扬声器的同一段数据关联，记录 24 kHz 输入、16 kHz 参考、重采样延迟、丢帧和欠载；参考不足时填零必须增加 `reference_underflows`。
- 离线测量使用 `tools/aec_acceptance.py measure`：提供 `mic_mix.wav`、`reference.wav`，若有同步的 `near_truth.wav` 和 AEC 输出 `clean.wav`，才计算 ERLE/残余 RMS。工具只测量，不实现 NLMS 或复制 AEC 算法。
- 通过条件：参考帧与麦克风帧在同一时间窗对齐、欠载为 0、输出帧数与输入帧数相等，且重复测试的 ERLE 有稳定改善。没有近端真值时只能报告相关性和 RMS，不能填写 ERLE。
- `reference=software_pcm24` 或 `digital_playback` 的结果标记为“数字参考 AEC”，不能写成“硬件回采通过”。

### 3. 真人插话层（声学能力，必须实机）

- 固定扬声器音量、设备距离和噪声条件；播放连续语音时，真人在 0.5 m、1 m 两个距离各插话 10 次。
- 记录插话起始时间、设备是否进入 listening、首个可识别词延迟、漏检、误触发、重复触发、播放是否被截断，以及主观可懂度。
- 至少做远端语音独播、近端语音独播、远端+近端重叠三组；每组保留设备日志和 WAV（若固件支持导出）。
- 必须有真实播放和真人插话记录；如有同步三路样本应一并测量。软件参考同样可以通过声学验收，不能仅因没有硬件回采判定能力不存在，也不能仅凭参考计数推导通过。

## 现有证据与边界

| 证据 | 能说明什么 | 不能说明什么 |
|---|---|---|
| `firmware/assistant_espidf/main/audio_io.cpp` | ES8311 输入输出、24 kHz codec、软件 PCM 参考环和 ESP-SR `aec_process` 的数据路径 | 不能说明扬声器声学回采存在 |
| `firmware/assistant_espidf/managed_components/waveshare__esp32_s3_touch_amoled_1_8/include/bsp/esp32_s3_touch_amoled_1_8.h` | TX/RX 引脚和 ES8311 同一 I2S 外设 | `duplex` 不等于参考通道 |
| `firmware/assistant_espidf/managed_components/espressif__esp-sr/include/esp32s3/esp_aec.h` | AEC 仅支持 16 kHz，要求 `refdata` 为扬声器发送的参考帧 | 不提供板级回采硬件 |
| `firmware/assistant_espidf/managed_components/espressif__esp-sr/include/esp32s3/esp_afe_sr_iface.h` | AFE 最后通道在有参考时才是 playback reference | 当前单麦配置没有第二通道 |
| `evidence/device-network-20261029.log` | 记录 `total 1 channels(1 microphone, 0 playback)`、I2S TX/RX 24 kHz | 不是自然双讲验收 |
| `evidence/audio-continuity-0.1.1-20261029.json` | 受控合成播放的缺帧/溢出统计 | 没有真人听测或回声减量结论 |

## 没有硬件 loopback 时能做什么

可以做：唤醒、单轮录音、轮流对话、播放后继续监听、按键或协议打断、数字参考 AEC 的初始化/计数/离线指标、静音和欠载监控。  
不能承诺：播放中任意时刻自然插话、远场回声抑制、不同音量/摆位下的稳定 ERLE、声学路径变化后的收敛。播放时暂停采集或只在播放结束后恢复，仍是合理的轮流交互策略。

## 设备日志字段建议

每一条 `aec_stats` 建议使用一行键值日志，便于串口采集：

```text
aec_stats enabled=1 reference=software_pcm24 sample_rate=16000 frame_ms=32 mic_frames=... reference_frames=... output_frames=... reference_underflows=... capture_overflows=... mic_rms=... ref_rms=... clean_rms=...
```

同时记录 `session_id`、`generation`、播放开始/结束时间、音量、参考队列深度、AEC 初始化错误码和设备固件 SHA。`reference=hardware_loopback` 只有在原理图/引脚说明和实机波形都能对应时才可填写。

## 判定模板

- 软件初始化：`PASS` / `FAIL`
- 数字参考算法链路：`MEASURED`（附 ERLE、欠载、帧对齐）/ `NOT MEASURED`
- 真人插话声学：`PASS` / `FAIL` / `NOT ACCEPTED`
- 总结：没有实际双讲声学测试时，不得写“自然双讲通过”；应写明所用参考来源和测试条件。

