# 内部内存手动语音基础实现

日期：2026-11-10，Asia/Shanghai。本批落实上一轮无PSRAM审计后的音频／协议／显示基础，不新增官方板型。AtomS3＋Echo Base继续inventory_only；两项隔离配置通过编译、镜像和链接检查，只证明基础路径可编译，不能替代AtomS3目标或实物验收。

## 最终改动

显式 `ASSISTANT_BOARD_INTERNAL_RAM=1` 选择内部内存模式，并要求最终 `CONFIG_SPIRAM=n`；没有该宏而只关闭PSRAM仍在CMake阶段拒绝。官方native registry与各板defaults未修改，现有板型默认仍用原PSRAM／AFE／唤醒路径。CMake不接受有屏文本模式缺少14px中文字库、10px标题字体的配置。

`board_profile.h`集中声明manual_audio_only、working_memory_caps和text_only。文本呈现独立于monochrome硬件能力：彩色LCD仍是彩色屏，只使用现有文本卡片呈现路径。沿用绑定码、错误、提醒／定时器、DND未读和按键卡片确认，不分配点云／导航点集；手动模式提示按住按键说话，不显示本地唤醒指令。

`audio_io.cpp`复用既有codec、FIR、PCM ring及generation／sample_index／discontinuity接口，在显式模式不初始化模型／AFE／AEC，不创建AFE任务。采样经FIR转16kHz，VAD标签标未知；原生输出目前限24kHz，直接播放，不建立AEC播放参考时间戳环。内部FIR容量512样本、采音输出ring 2,048样本。采音读失败、FIR容量失败和ring溢出锁存错误，由手动录音owner取消本轮；读取跨generation的PCM不进入新录音，也不会制造成功静音尾帧。

`control_protocol.cpp`保留原request／ready／PCM／end、媒体暂停归属、超时和断线处理，手动模式不缓存／上传待机麦克风PCM。它不声明或接受本地唤醒、wake_clip、自然插话及媒体语音唤醒能力，AEC reference声明none。手动发送仍使用已有wake命名的sender任务与队列；没有删掉这条手动上传链路。队列满／音频不连续／采音错误通过原session_end取消逻辑退出，不覆盖旧PCM后继续报成功。

两处自然插话／媒体唤醒的人工决策标记及对应逻辑保持逐字一致。手动模式在进入这些自动语音分支前退出；现有默认路径仍执行原逻辑。未修改headless_ui的无屏绑定码人工决定、网关或运行时。

## 内存请求与实际链接边界

| 协议启动请求 | 旧默认路径 | 显式内部内存模式 |
| --- | ---: | ---: |
| 唤醒前缀／后缀 | 192,000字节 | 不分配 |
| 发送队列 | 401×660＝264,660字节 | 33×660＝21,780字节 |
| 播放队列 | 32×2,060＝65,920字节 | 8×2,060＝16,480字节 |
| 插话前滚 | 19,200字节 | 不分配 |
| 四个协议任务栈 | 32,768字节 | 32,768字节 |
| **上述合计** | **574,548字节** | **71,028字节** |

任务栈保留原8KiB要求，没有依据链接成功任意缩栈。71,028字节不含采音producer 6,144字节栈、Wi-Fi／TLS／WebSocket、LVGL／DMA、任务控制块或配网／OTA峰值；提示音仍按需需要19,200字节PCM和8,192字节任务栈，改用内部内存并检查分配失败。

发送队列预留一个控制槽。32个音频槽最多容纳20,480字节payload；按20ms／640字节帧计算最多640ms，实际采音producer每10ms供帧或分包更短时可能更早填满。服务端ready／网络背压超过容量时本轮明确取消，不保证原4秒ready超时内始终能缓存全部声音；4秒是墙钟上限，缓存容量是独立上限。这需要真实网关录音握手及慢网验收。

两个临时参考构建的实际ELF符号不包含esp_srmodel_init、afe_config_init、aec_create、g_reference_due_us／g_reference_ring或heap_caps_malloc_extmem_enable；外部RAM `.bss`／`.data`均0。链接脚本仍有 `.ext_ram.dummy` NOLOAD段，用于跨总线地址预留以跳过flash rodata，不是PSRAM数据分配；保存sections.ld解释，未把它当作外部RAM运行证据。

| 配置（全部是缓存构建） | 应用字节 | DRAM data | DRAM BSS | 外部RAM数据／BSS |
| --- | ---: | ---: | ---: | ---: |
| M12无屏参考＋临时internal宏／PSRAM关闭 | 1,134,608 | 19,492 | 57,480 | 0 |
| EDA TV彩色文本参考＋临时internal宏／PSRAM关闭 | 1,692,784 | 20,484 | 129,312 | 0 |
| 1.43C官方默认PSRAM回归 | 2,544,944 | 34,096 | 103,584 | 137,332 |

彩色文本参考BSS包含64KiB的LVGL内存池；不把这个参考的可用heap推算为AtomS3的运行余量。当前还没有测启动、TLS重连、手动录放音、提示音或OTA的internal_free／largest_block／栈水位，因此AtomS3不登记为已支持。

## 实际构建命令与失败修正

隔离参考基于已有缓存profile，使用prepare_cached_firmware_build.py分别准备，随后只在stage的registry给选中profile追加internal宏，并在stage配置关闭PSRAM。工作区registry不含该临时覆盖，未把参考配置计成官方新板。

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=m5stack-atoms3r-cam-m12-echo-base reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lceda-course-examples-eda-tv-pro reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-touch-amoled-1.43c reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -S --size-sort -C build/<app>.elf
xtensa-esp32s3-elf-size.exe -A -d build/<app>.elf
```

三组最终构建、镜像／nm／section命令均退出0；8／16／8MB镜像项目身份与checksum／validation hash一致。临时M12选中headless_ui和manual_wake_word；临时EDA TV选中display_ui文本路径和manual_wake_word；1.43C默认仍选原display／wake_word及AFE／AEC符号。每组161个main文件集合与最终工作区一致，编译宏、生成配置、临时registry覆盖、板级分区来源、完整输入／归档摘要均检查。

EDA TV初次编译缺14px CJK和10px标题字体失败；增加配置检查后又按预期在配置阶段拒绝。原因是已有sdkconfig覆盖新增defaults，仅追加defaults不会修改已保存的字体选项。只修正隔离sdkconfig的两项字体为y后完整构建通过。失败日志、当时源和配置均保留。随后实际同步最终32音频槽及能力声明源码，再分别增量构建，最终记录对应当前文件。

另用第三个全新隔离M12配置，只设置PSRAM=n、不加internal宏执行reconfigure，退出1并命中明确的unsupported错误；说明原保护仍在。该拒绝不是M12或AtomS3目标构建失败，也没有实物操作。

总记录 `firmware/assistant_espidf/internal_ram_manual_builds_20261110.json` 包含两个私有参考、一个默认回归、检查命令、覆盖摘要和阶段失败。只有默认1.43C生成标准官方缓存记录 `cached_build_attempts_20261110_internal_ram_waveshare-esp32-s3-touch-amoled-1.43c.json`；两个临时覆盖未进入官方通过计数。完整证据在忽略目录 `evidence/internal-ram-manual-20261110/`。

本轮复用既有IDF5.5.6-dirty／17组件override和Python offline_guard，没有安装依赖。未运行测试套件／独立类型检查、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。

## AtomS3后续

仍需接入实际AtomS3的两种屏控ID、SPI3／PWM16、I2C1底座和无MCLK的ES8311时钟；GC9A01组件依赖尚未准备。应在实际128×128目标上校验文本卡片、绑定码与物理按键，再验证录音握手／慢网取消和整个内部内存阶段峰值。不能以当前参考板编译成功宣称AtomS3、其他无PSRAM芯片或实物通过。

总矩阵仍96官方源码变体／97本地入口含1历史／92待源码，累计53官方变体曾缓存构建通过；当前1 passed（默认1.43C）、52 stale、6历史failed、129 not_run（其中37有源码），全部188实机not_run。上一轮no_psram_readiness_20261110.json是改动前的历史资源基线，本批新账本记录当前实现与尚未满足的实机门槛。

收尾实际执行 `git diff --check` 退出0；15项新增／变更文件逐项 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1为新增差异。三组各161个main文件及输入／归档摘要匹配，两个私有配置外部RAM数据／BSS为0；6项保护文件、完整官方registry及两处人工决策逻辑保持原摘要／原文。只更新既有AtomS3 research，矩阵摘要与96官方源码／AtomS3 inventory_only／188实机not_run一致。四个最终构建／配置进程均已结束，未将参考配置计入官方支持。
