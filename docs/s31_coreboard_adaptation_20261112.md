# ESP32-S31 功能核心板源码候选（2026-11-12）

新增 `espressif-esp32-s31-function-coreboard-1` 源码入口，并将构建／烧录身份读取器的目标范围扩展到ESP32-S31。固定上游将此机型标为preview，要求IDF≥6.1；候选保留完整本地唤醒、联网语音、媒体、OTA和手机绑定路径。**目标SDK未安装，未做S31编译或实机验收。**

## 硬件与芯片配置

原板四份来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的README、config.h、config.json和实际类，按Git blob核对。它没有显示屏，使用单颗ES8311和NS4150B，Wi-Fi联网：

| 信号 | GPIO |
| --- | --- |
| I²C0 SDA／SCL | 51／50 |
| I2S0 MCLK／BCLK／WS | 52／53／55 |
| I2S DIN／DOUT | 54／56 |
| 功放使能 | 57 |
| BOOT | 61 |
| WS2812 RGB灯 | 60，候选未驱动 |

16MB Flash、八线PSRAM；芯片配置参考同提交 `sdkconfig.defaults.esp32s31`，选择QIO Flash80MHz、CPU320MHz、OCT PSRAM200MHz。IDF release/v6.1公共头文件固定提交 `f0114250e205e33a945e2f3e7ec40085c5a029a2`，soc_caps声明双核、62个GPIO，50–57／60／61位于其有效范围；PSRAM Kconfig支持所选模式。这些是源码和接口依据，不是实际板卡容量、频率或电气运行证明。

独立应用名 `assistant_s31core`，manifest限制 `>=6.1.0,<6.2.0`；公共→芯片→板型三层配置继续由既有构建路径加载。Python、PowerShell、CMake三个身份读取器同时接受S31，其他芯片仍受实现白名单限制；没有仅靠删除芯片检查来给C3／C5／C6生成空壳profile。

## 无屏绑定、唤醒和音频

复用已有headless UI／热点配网页，设备在音频初始化前即可显示六位绑定码。BOOT61先松开，再长按三秒重新开启手机查看窗口；网页提示与实际手势一致。语音输入采用唤醒词，BOOT不同时承担按住说话，避免长按打开查看窗口时发起录音。

单ES8311使用已存在的checked data／codec实现，配置BOTH、一个IN_OUT设备、STD mono16／24kHz、I2S0及MCLK。共享输入输出句柄在失败清理时只释放一次，实际set_fmt／enable错误参与打开状态检查；麦克风增益和DAC静音检查真正回调结果。PA57在UI／绑定前关闭，使用INPUT_OUTPUT保留GPIO读回，codec PA设NC；公共路径在数字静音成功后才启PA，关闭时先切PA。没有ES7210或TDM4分支。

ESP-SR精确2.4.7的Kconfig支持S31 mn7，库中也包含MultiNet7符号。实际nm和objdump已保存：S31模型分派函数包含实现，C3／C5／C6对应函数则直接返回空。候选给S31明确选择mn7_cn，缺少时停止，保留“你好，小燕子”自定义词及现有PCM软件AEC、WS媒体／回执和同板OTA；不回退到S3模型或纯手动模式。存在模型代码仍不证明S31运行速度、识别率或声学效果。

原板说明单麦克风没有硬件播放参考，因此不宣称硬件AEC。本项目仍沿原有软件PCM参考路径，其回声效果、重开／静音、模型并发和可用堆须实物测量。RGB灯、YT8531以太网、相机、SD、电池及原厂手势未实现，GPIO60未按普通LED驱动。

## 实际执行

S31实际只读预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile espressif-esp32-s31-function-coreboard-1 --destination I:/xy_board_builds_20261109/s31core_review_espressif-esp32-s31-function-coreboard-1_20261112 --idf-path C:/Espressif/official-5.5.6/frameworks/esp-idf-v5.5.6 --check-only
```

退出1：SDK5.5.6不满足 `>=6.1.0,<6.2.0`。没有创建stage、解析完整依赖或生成S31镜像。IDF6.1、编译器、Wi-Fi／PSRAM库与依赖仍需准备，未安装任何组件。

Python `load_board_profiles()`与PowerShell `Get-AssistantBoardProfiles`实际读取成功，均146个本地profile，Python返回S31目标名；这只是登记表读取，不是编译。145个旧profile对象保持原值，本地宏条件下145项旧音频文本比较一致；该脚本不展开SDK／include，不当作预处理证明。

两项已授权旧板缓存回归实际终态均退出0：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-rlcd-4.2 reconfigure build
```

立创app2,263,824字节、RLCD2,228,000字节，各3MB；model均2,203,819字节／4MB。每项230个main文件、238项输入摘要与最终工作区匹配，23产物归档。实际image_info、nm、size、SDK分区解码均退出0，核对身份／16MB／NVS／双OTA／model与codec及FT或TE符号。使用既有IDF5.5.6-dirty、17个override、offline_guard、每stage并发度2；**没有编译S31或使用IDF6.1**。

`tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory`退出0；`close_batch.py`核对17份来源、模型库检查日志、旧profile和保护摘要、目标预检、最终快照、构建产物及清单；`git diff --check`退出0。控制协议人工决策、OTA、配网、headless_ui、display_ui和CoreP4板级／显示七文件未改。全部证据位于 `evidence/s31-coreboard-source-20261112/`。

## 剩余板型的唤醒限制

本批另外读取四个C5板型的12份固定config／README／config.json，继续保持inventory_only。对本地精确ESP-SR2.4.7的C3、C5、C6三个 `libmultinet.a` 实际执行 `objdump -d --disassemble=esp_mn_handle_from_name`，均显示 `li a0,0; ret`，并记录库与日志SHA256。Kconfig中文MultiNet选择也不包括这三种芯片；不能用库文件存在或通用函数声明推断它们支持当前自定义唤醒词。

已向牛牛询问是否接受固定WakeNet词；未收到选择前保留当前唤醒词要求，不静默降级按键语音，也不把其计入已适配。该决策不影响S31本批推进。完整原文和反汇编在 `evidence/c5-readiness-20261112/`；四个C5研究记录同步矩阵，硬件实现／模型或SDK尚未完成。

当前145个官方源码入口＋1历史，共146本地profile；43官方变体待源码。S31族四项中一候选／三待实现，P4族27项仍全为待目标构建的源码候选。累计64官方变体曾缓存通过，当前2 passed／62 stale／6历史failed／118 not_run（75有源码）；188项本轮实机全not_run，唯一现有微雪1.8不能代替S31。

本轮未运行测试套件、安装SDK／依赖、启动Docker、写Git、刷机、OTA、上传或发布，全量目标继续进行。
