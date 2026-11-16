# AtomS3＋Echo Base无PSRAM前置审计

日期：2026-11-10，Asia/Shanghai。本批没有把AtomS3登记为已适配：它继续inventory_only，96个官方源码变体的计数不变。实际进展是量化当前引擎为何不能直接用于无PSRAM板，并在CMake配置阶段明确拒绝这种尚未实现的组合；完整手动语音／内部内存路径仍待开发。

## 真实硬件与已有复用边界

固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `main/boards/m5stack/atoms3-echo-base/` config、README和板类明确：8MB Flash、`CONFIG_SPIRAM=n`。README说明没有本地唤醒和音频前处理，底座需从Echo Base USB-C供电。它与已有AtomS3R的PSRAM／LP5562配置不同，不能套用同名近邻板。

128×128屏幕SPI3 MOSI21／CLK17／CS15／DC33／RST34，GPIO16高有效PWM；按钮41。固定类通过RDDID检测GC9107和ST7735：GC9107 ID低24位079100、gap(0,32)、无mirror／invert；ST7735低16位7683或897c、gap(2,3)、双mirror／invert。未知ID原厂回退GC9107，本地已有AtomS3R检测封装则停止未知ID；新板接入时需明确采用并记录行为，不能只删掉GC9107分支来获得构建成功。

ES8311底座使用I2C1 SDA38／SCL39，BCLK8／WS6／DIN7／DOUT5，MCLK NC、BCLK派生时钟。已有 `atomic_echo_base.cpp` 可复用只探测0x18与0x43、PI4IOE ID校验与P0功放掩码更新，不需复制原类全地址扫描、整端口写和重启等待。AtomS3R显示LP5562／内部I2C0 SDA45／SCL0不属于AtomS3，不能一起套入。

固定四项公开来源原文／URL／SHA-256已归档在 `evidence/atoms3-no-psram-20261110/upstream/`。更新原有AtomS3 finding，没有重复追加，没有新增native profile。

## 符号测量与不可行的直接移植

使用上一批已实际编译的1.43C ELF读取共享协议层的真实ABI大小。当前工作区 `control_protocol.cpp` 与该构建stage逐字节摘要相同；此处只用它测量共享结构布局，绝不把1.43C的PSRAM、剩余内存或运行能力当成AtomS3硬件证据。

实际命令：

```powershell
xtensa-esp32s3-elf-nm.exe -S --size-sort -C I:\xy_board_builds_20261109\xy_cached_a9d2797d00c7\build\assistant_ws143c.elf
```

退出0，结果：`g_incoming_chunk`为0x80c＝2,060字节，`service_wake_audio_tx()::chunk`和`::next`均0x294＝660字节。不是用宿主64位C++ sizeof猜ESP32布局。

| 协议启动分配 | 实际请求字节 | 依据 |
| --- | ---: | --- |
| 唤醒后缀缓存 | 64,000 | 16kHz／16位／2秒 |
| 唤醒前缀缓存 | 128,000 | 16kHz／16位／4秒 |
| 发送队列 | 264,660 | (400＋1)×660，结束控制项也占槽 |
| 播放队列 | 65,920 | 32×2,060 |
| 插话前滚缓存 | 19,200 | 16kHz／16位／0.6秒 |
| 四个协议任务栈 | 32,768 | screen_actions／wake_pcm_tx／capture_pcm／playback_pcm各8,192 |
| **仅这些启动分配合计** | **574,548** | 比512KiB整颗SRAM还多50,260字节 |

512KiB只是芯片总SRAM上限，可用于data／heap的内部内存还要扣除IRAM、系统保留和静态数据。因此这个下界已足以否定“把MALLOC_CAP_SPIRAM改成INTERNAL即可支持”的方案；不能把574,548当成完整运行峰值。

此外，已链接音频FIR、AEC pending／original、AFE输入／输出环、播放参考ring／时间戳及临时reference共115,280字节静态数据，当前位于外部RAM。将EXT_RAM_BSS_ATTR在无PSRAM下变成普通BSS不会减少这些数据。AFE／AEC／模型动态分配、音频两任务14,336字节栈、Wi-Fi／lwIP／TLS／WebSocket、LVGL／卡片／DMA、任务控制块等均未计入上表。提示音还按需申请19,200字节PCM和8,192字节任务栈。

协议中叫wake的发送队列同时承载 `manual_start`／ready确认／PCM／`manual_end`。删除所有“wake”队列会破坏手动录音；关闭 `wake_word_init` 也不能取消其余启动分配。Guition的manual_wake_word入口只是没有模型，它仍创建PSRAM的AFE、队列和显示资源，不能当成现成的无PSRAM实现。

点云路径还会申请粒子、投影、正弦表、双画布和导航图标集；现有无屏UI虽然没有点云，其协议／音频仍使用PSRAM。必须按拥有模块一起处理，不在板级宏里只关一个显示入口。

## 已落代码与真实配置验证

仅修改 `firmware/assistant_espidf/main/CMakeLists.txt`：在非early-expansion阶段，若最终 `CONFIG_SPIRAM` 关闭，明确报错说明内部内存音频、协议队列和显示尚未实现。该保护是当前引擎能力约束；未来完成无PSRAM路径后应由真正的能力检查替换，不能以删除保护冒充完成适配。

先用现有可缓存的M12 profile创建全新隔离目录，复制17个已安装组件；仅在隔离defaults末尾追加 `CONFIG_SPIRAM=n`，未修改工作区各板defaults或旧构建目录。实际命令：

```powershell
python -B tools/prepare_cached_firmware_build.py --profile m5stack-atoms3r-cam-m12-echo-base --destination I:\xy_board_builds_20261109\atoms3-internal-memory-probe-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=m5stack-atoms3r-cam-m12-echo-base reconfigure
```

准备退出0，配置退出1，生成sdkconfig确认PSRAM关闭；CMake明确命中 `Native engine requires PSRAM`，未进入固件编译。这个结果是对配置保护的预期拒绝，**不是AtomS3目标构建失败，也不是M12硬件不能工作的证据**。没有创建AtomS3 profile，矩阵AtomS3 build继续not_run。

正向回归同步1.43C的当前源并执行：

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-touch-amoled-1.43c reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/assistant_ws143c.bin
xtensa-esp32s3-elf-nm.exe -C build/assistant_ws143c.elf
```

均退出0，仍为8MB `assistant_ws143c`，checksum／validation hash有效，所选CO5300／CST／双codec符号不变。代码只改CMake保护，未声称重新编译全部未改运行期源。应用仍2,544,880字节、单OTA槽剩207,632；模型2,203,819字节、模型分区剩155,477。最终161个main文件集合／168项输入摘要与工作区一致，17项构建归档产物，记录为 `cached_build_attempts_20261110_atoms3_ram_waveshare-esp32-s3-touch-amoled-1.43c.json`。

资源账本为 `firmware/assistant_espidf/no_psram_readiness_20261110.json`，包含实际符号、分配下界、源码摘要、排除项、负向配置／正向构建证据。日志、配置、参考ELF和脚本保存在忽略目录 `evidence/atoms3-no-psram-20261110/`。本次仍使用既有IDF5.5.6-dirty、17个local override和Python offline_guard，没有安装依赖。

## 必须一起实现的下一步

1. 音频：保留ES8311驱动与FIR，建立手动PCM16采集，不启动AFE／AEC／WakeNet；保留generation、连续性、松键时有限尾帧和显式错误处理。没有AEC不能报告自然插话或媒体唤醒可用。
2. 协议：为内部内存限定播放和手动发送容量，保留手动request／ready／end握手与媒体暂停归属，不持续上传待机麦克风。缓冲不足／断流必须明确取消本轮，不覆盖或悄悄丢PCM。容量还要与ready等待时间和网络背压一起预算，不能只按“20ms一帧”缩小。
3. 显示：提供有界文本界面或另行量化的小动画，保留六位绑定码、错误、卡片、提醒、DND未读状态和按键确认；无本地唤醒的板不能继续显示“说你好小燕子唤醒”。它仍是彩色LCD，不将文本模式伪装成OLED硬件能力。
4. 基础内存：同时处理协议／采音／播放／提醒任务栈、可选cue、静态reference／FIR／队列、WebSocket／TLS及OTA／配网的阶段峰值。不要把所有PSRAM BSS机械迁移内部RAM，也不要在没有依据时缩小既有栈。
5. 板级：保留GC9107与ST7735两套真实ID路径、AtomS3的PWM16和I2C1底座、BCLK派生时钟及P0功放。GC9A01组件仍不在安装缓存中；依赖准备与无PSRAM源码是两个独立阻塞。
6. 验收：目标真实编译／链接，检查没有外部RAM段或仍被执行的PSRAM分配、正确的8MB分区和能力声明；随后才能测启动／配网／TLS重连／录放音／提示音／OTA各阶段的internal_free、largest_block和任务栈水位。现有PSRAM目标回归不能证明这些指标。

上述是实施门槛，不是已完成的新功能或已获实机验收。当前仍有其他未适配板可继续推进，任务保持进行中；不会因这一块缺基础能力而把全部目标标成blocked或complete。

实际刷新矩阵退出0：仍96官方源码／97本地入口含1历史／92待源码，累计53变体曾缓存构建通过；当前1 passed（1.43C）、52 stale、6历史failed、129 not_run（其中37有源码），全部188实机not_run。未执行测试套件／独立类型检查、依赖安装、宿主长期服务、Git写操作、上传、发布、刷机或OTA；运行期音频／协议／显示和人工决策代码未改。

收尾实际执行 `git diff --check` 退出0；7项新增／变更文件逐一 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1为新增差异。资源账本10项输入、4项公开原文及归档日志／配置／ELF摘要匹配；正向构建168项输入／17项归档产物匹配。8项保护文件和完整native registry摘要未变，只更新既有AtomS3 research；矩阵research／registry摘要、AtomS3 inventory_only／build not_run及全部188实机not_run匹配。两个配置／构建进程均已结束。
