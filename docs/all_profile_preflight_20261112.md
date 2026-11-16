# 全部本地板型入口预检与ML307带宽核对（2026-11-12）

本批对118个已登记本地入口逐一执行真实只读预检，其中117个映射官方发布变体，另1个是历史微雪兼容入口。没有把尚无实现的71个官方变体放进S3构建脚本。固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的实际发行范围仍为154目录／188变体。

## 工具改动与实际执行

`tools/prepare_cached_firmware_build.py`新增 `--check-only`。它复用原来的SDK完整性／版本、BSP存在性、直接组件缓存和精确版本检查，在任何构建目录创建或组件复制前返回。既有暂存模式保留；该选项不运行依赖解析、安装、固件构建或刷机。

118项实际命令逐项保存于 `evidence/all-profile-preflight-20261112/results.json`，格式如下，PROFILE和DESTINATION均为每项真实参数：

```text
C:/Users/29248/.espressif/python_env/idf5.5_py3.12_env/Scripts/python.exe -B tools/prepare_cached_firmware_build.py --profile PROFILE --destination I:/xy_board_builds_20261109/preflight_only_20261112_PROFILE --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

原始输出和完整异常堆栈保留在同目录logs；每项记录退出码、明确原因、日志SHA256、单板manifest摘要、SDK基础版本来源和准备工具摘要。117项官方结果如下：

| 首个预检结果 | 官方入口数 | 含义 |
| --- | --- | --- |
| 通过当前直接缓存检查 | 64 | SDK与当前检查覆盖的直接缓存条件满足；退出0，未暂存或编译 |
| 缺直接组件 | 42 | 退出1，先遇到的直接依赖不在当前缓存 |
| 缺BSP／未找到唯一BSP | 9 | 退出1，当前缓存无法提供所选BSP |
| 精确版本不符 | 1 | SH8601原版要求1.1.4，当前同名BSP缓存为2.0.3；退出1 |
| SDK版本不符 | 1 | NT26要求>=6.0.1,<6.2.0，本机为5.5.6；退出1 |
| 合计 | 117 | 53项被首个明确条件挡住；无意外失败 |

历史兼容入口另有1项预检通过，所以全部本地结果是65通过／53阻塞。所有118个指定暂存目录均未创建。**这里的64项通过是直接预检结果，不是本轮新增64次固件构建。**当前已有构建证据仍是累计64个官方变体曾缓存编译通过，其中2项与当前输入匹配、62项历史证据已过期。

预检只报告每项首个阻塞，精确版本按 `x.y.z[~revision]` 比较；版本范围、条件依赖、完整传递解析、SDK提交／dirty状态、组件API兼容、编译链接尺寸和实机运行均不能据此验收。原始JSON的 `dependency_resolution`、`compilation`、`device_acceptance`均保持not_run。固件侧索引为 `native_dependency_preflight_20261112_all_profiles.json`。

## 需要准备的直接依赖

进一步按全部本地manifest静态合并，共46组“组件＋声明要求”：26组注册表组件当前缺失、1组要求固定Git源码、3组与当前缓存精确版本不符、12组精确匹配、4组为本次工具未判断的版本范围。这是直接要求清单，不是完整解析后的依赖锁。

下面各组的受影响板型可在 `evidence/all-profile-preflight-20261112/direct-dependency-catalog.json` 查看；同一板可出现在多组，不能把组的板数相加作为阻塞总数。

| 当前缺少的注册表组件 | 声明版本 |
| --- | --- |
| 78/esp_lcd_nv3023 | 1.0.1 |
| 78/uart-eth-modem | 0.7.0（另需SDK>=6.0.1） |
| esphome/esp-hub75 | 0.3.6 |
| espressif/esp-box | 3.1.0~2 |
| espressif/esp-box-3 | 3.2.1 |
| espressif/esp-box-lite | 2.1.0~2 |
| espressif/esp_lcd_axs15231b | 2.1.0 |
| espressif/esp_lcd_gc9a01 | 2.0.4 |
| espressif/esp_lcd_ili9341 | 2.1.0 |
| espressif/esp_lcd_spd2010 | 2.0.0~1 |
| espressif/esp_lcd_st77916 | 2.0.2 |
| espressif/esp_lcd_st77922 | 2.0.2 |
| espressif/esp_lcd_st7796 | 1.3.5、1.4.0（按板隔离） |
| espressif/esp_lcd_touch_gt1151 | 1.1.0~1 |
| espressif/esp_lcd_touch_gt911 | 1.1.3、1.2.1（按板隔离） |
| espressif/esp_lcd_touch_spd2010 | 2.1.0 |
| espressif/esp_lvgl_adapter | 0.6.0 |
| espressif/iot_usbh_rndis | 0.3.1 |
| espressif/m5stack_core_s3 | 3.0.2 |
| waveshare/custom_io_expander_ch32v003 | 2.0.0 |
| waveshare/esp32_s3_touch_amoled_1_75 | 3.0.1 |
| waveshare/esp32_s3_touch_amoled_1_75c | 3.0.0 |
| waveshare/esp32_s3_touch_amoled_2_06 | 2.0.0 |
| waveshare/esp32_s3_touch_amoled_2_16 | 2.0.1 |

另有微雪1.32 BSP的单板manifest指定Git提交 `22733e088a533378680a03e398279b2f7ab09a73`；它是源码修订，不是组件版本号，不能仅用同名注册表缓存代替。具体Git地址和源码要求保留在目录JSON与原manifest。

当前同一组件有真正不同精确版本要求的四组为：

- esp_codec_dev：1.1.0／1.3.1／1.5.11，当前缓存1.5.11。
- esp_lcd_st7796：1.3.5／1.4.0。
- esp_lcd_touch_gt911：1.1.3／1.2.1。
- 微雪1.8 BSP：1.1.4／2.0.3，当前缓存2.0.3。

这些版本应在各板独立环境解析，不能通过覆盖统一缓存让所有板“看起来通过”。其他不同范围字符串可能存在交集，不把字符串不同直接判为冲突。本批只列出牛牛后续需要准备的条件，没有安装、替换或降级依赖。手头“微雪1.8”也不能仅凭尺寸认定为SH8601原版或CO5300 V2，实机入口仍须按硬件修订核对。

## ML307的实际带宽限制

本批继续核对ML307接入时，确认当前协议上行声明为16kHz单声道PCM16，下行语音和媒体使用24kHz单声道PCM16。固定上游Ml307Board要求串口921600；精确 `esp-ml307 3.7.5` 的Ml307Tcp用十六进制发送，并对接收的rtcp数据解码十六进制。串口按8N1每字符10位计算。

| 方向 | 原始PCM字节／秒 | AT十六进制字符／秒 | 不计任何额外开销的最低波特率 |
| --- | --- | --- | --- |
| 16kHz上行 | 32,000 | 64,000 | 640,000 |
| 24kHz下行 | 48,000 | 96,000 | 960,000 |

24kHz下行每20ms有960字节PCM，单十六进制数据就需1920个串口字符，在921600下至少耗时20.833ms，超过实时帧间隔。TLS、WebSocket、URC、命令与确认开销只会增加工作量；零开销下最多承载23,040个PCM16采样／秒。全双工UART应分别判断两方向，不能把上下行相加后再判定单方向容量；这里仅下行已经超过限制。

这是固定ML307Tcp十六进制路径的理论下界，不是实机吞吐测量，也不自动适用于AtModem可能识别出的EC801E或其他传输方式。证据、源码快照、输入和计算结果在 `evidence/all-profile-preflight-20261112/ml307-pcm-bandwidth.json`。

因此后续完整ML307适配还需：核实模块／板级支持的更高UART速率，或实现终端与桥接端共同支持的压缩音频，再补齐此前记录的TLS验证、有界缓冲、确认／超时和掉线恢复。不能仅写TCP封装就宣称能持续播放现有PCM媒体，也不通过擅自降低采样率或移除媒体来结束全量目标。8个待源码ML307变体继续inventory_only。

## 交付与剩余范围

本批改动是只读预检选项、全部本地入口的真实预检记录、直接依赖目录和ML307带宽证据。现有固件C++、板型登记和控制协议人工决策保持原摘要。`git diff --check`退出0；前批两个当前构建的424项输入摘要与42份归档产物另做一致性检查，没有重复运行固件构建或测试套件。

117个官方变体有源码入口、71个待源码的总量不变，全部188项本轮实机not_run。当前只有现有微雪1.8实物；对应硬件、SDK、依赖和实机验收的缺口继续保留。未安装依赖或SDK、写Git、启动长期服务、刷机、OTA、上传或发布。
