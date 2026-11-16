# MixGo Nova 与 Magiclick 2.4／2.5 适配（2026-11-10）

后续更新：本页保留此前候选范围；Magiclick2.4与星智0.85WiFi现已移除各自外部NV3023依赖，保留原默认25／专用54命令和reset时序，并分别完成首次整板构建。最新来源与实机边界见[本批记录](nv3023_sdk_20261112.md)。

后续更新：本页保留此前候选范围；AtomS3R EchoBase/Pyramid与Magiclick2.5现已移除各自外部GC9A01依赖，保留原GC9107专用表和识别，两款AtomS3R首次整板构建通过，Magiclick整板仍受GCC内部错误阻塞。最新来源与实机边界见[本批记录](gc9107_sdk_20261112.md)。无PSRAM AtomS3仍独立受阻。

本批为小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的三个实际发布变体增加基础Wi-Fi语音源码入口。读取config、实际板类和音频类，不按屏幕大小猜引脚；官方清单仍为154目录／188发布变体。只有现有微雪1.8实物，三款新板均无实机验收。

## MixGo Nova

独立应用 `assistant_mixgo`，profile `mixgo-nova`。固定发布配置明确8MB Flash、QUAD PSRAM；不能套用16MB／Octal默认值。板级分区复用已有8MB布局：两段0x2A0000应用分区，模型0x5C0000／0x240000；NVS／OTAdata保持既有布局。网络音频仍PCM24，软件AEC沿用现有管道。

- 屏幕128×160，SPI3 mode0／40MHz，MOSI40／CLK41／DC18／CS45／RST NC／背光14高有效；RGB、反色关闭、双轴镜像、gap2／1。固定配置名为ST7735，实际板类调用ST7789兼容驱动，本地按实际构造接入，复用128×160布局和中文小屏字体。
- ES8374，I2C0 SDA37／SCL36；I2S0 MCLK35／BCLK34／WS47／DIN33／DOUT48，24kHz、256fs主时钟、初始stereo16、实际输入／输出各mono16，PA NC。两handle共享一个codec且关闭时保持codec使能；现有状态锁串行化录放音生命周期。
- 固定README说明麦克风输入需将寄存器0x21的选择从0x14改为0x24。本地在每次真正打开音频后，按 `(old & 0xcf) | 0x24` 读改写并完整回读；错误向上传播，保留其他位。没有改安装组件或缓存第三方驱动，使修正仅作用于该板。
- 复用已有I2S数据错误记录及显式超时传输；增益、静音另外检查实际codec回调返回值。默认输入增益30dB，播放期间沿用共用12dB降增益；无硬件回采声明。
- BOOT0作为共用按住说话／无触摸操作卡输入，首次释放后才接受。上游无音量GPIO，不把NC当作按键；LED38及其他传感器未接入。

8MB容量、QUAD RAM和35／36／37接线须对应实物；编译不能证明麦克风输入选择、主从时钟、PGA／静音／重开、屏幕色序、按键、声学AEC或联网绑定可用。

## Magiclick 两种发布变体

| 项目 | Magiclick 2.4 | Magiclick 2.5 |
| --- | --- | --- |
| profile／应用 | `magiclick-2p4`／`assistant_magic24` | `magiclick-2p5`／`assistant_magic25` |
| 屏幕 | 128×128 NV3023，驱动默认初始化表 | 128×128，ADC检测2.5A GC9107或2.5A1 ST7735 |
| SPI3 | mode0／40MHz | mode0／20MHz |
| 引脚 | MOSI15／CLK16／CS17／DC18／RST14 | MOSI16／CLK15／CS14／DC18／RST17 |
| 方向与色序 | BGR、仅mirrorY、gap0／0、不反色 | GC9107：RGB／不镜像／gap0／0／不反色；ST7735：BGR／双轴镜像／gap2／3／反色 |
| 背光13 | 高有效 | 低有效，初始占空比1023保证关闭 |
| 音频 | ES8311，I2C0 SDA5／SCL6；MCLK8／BCLK9／WS11／DIN10／DOUT12；24kHz／256fs／单codec双向handle；PA4 | 同2.4音频接线 |
| 按键 | 说话21／音量加47／减0，低有效，首次释放与多键取消 | 同2.4按键 |
| 板级输出 | LED电源39高，关闭未实现的LED功能 | LED39高关闭，Wi-Fi模式下modem电源40低关闭 |

两款固定发布均未覆盖容量默认值，本地按该发布的16MB／Octal设置；这不证明任意未确认PCB或模块容量。2.4的PA4原始PCB无效、2.4A适用，不混称可控功放。充电48／电池ADC1预留；LED38、低电量、休眠、电源管理尚未实现。

2.5在初始化屏幕前读取ADC1 channel2（GPIO3），12bit／12dB取10次平均，检查配置、每次读取和删除unit结果。固定上游采用 `int(raw * 3300 / 4095)` 的名义值：小于100选择GC9107、大于3200选择ST7735，对应平均raw≤124和raw≥3973。**这些不是校准后的毫伏值**。未知125–3972时，本地停止显示初始化并记录raw，不沿用上游默认猜ST7735的回退；实际两种PCB的原始ADC余量仍待实测。

GC9107通过GC9A01组件的自定义vendor表接入，完整保留24条命令、两条14字节gamma和COLMOD05，零长度FE／EF按原表保留。固定GC9A01 2.0.4实现先发送SLPOUT并等待100ms，再发送MADCTL／COLMOD及自定义表，因此不重复注入另一套唤醒序列；其API及实现仅读取，没有安装。ST7735分支按固定板类使用ST7789兼容驱动。面板与LVGL最终镜像一致，背光极性分别处理。命令数据MIT许可和来源保留在 `main/third_party/xiaozhi_magiclick/`，不改整体许可证。

2.5仍只实现基础Wi-Fi语音，蜂窝ML307／网络切换、LED、电量／充电、休眠未完成。共用按住说话／操作卡／音量交互不等于原厂全部手势已移植。没有对应实物，屏幕和ADC分支不能验收。

## 依赖预检、构建和证据

两款Magiclick实际执行 `python -B tools/prepare_cached_firmware_build.py --profile <profile> --destination <I盘独立预检路径> --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`，均退出1：2.4缺 `78/esp_lcd_nv3023 1.0.1`，2.5缺 `espressif/esp_lcd_gc9a01 2.0.4`。没有创建隔离目录或进入编译，也没有安装依赖；准确参数／日志／摘要见 `native_dependency_preflight_20261110_magiclick.json`。两款仅为未编译源码候选，MixGo或其他板构建不能替代。

固定公开源代码、API及摘要归档于忽略目录 `evidence/mixgo-magiclick-source-20261110/`，研究事实和未完成边界记录在 `board_research_v2.5.2.json`。预检日志在 `evidence/magiclick-preflight-20261110/`。

实际使用本机IDF5.5.6-dirty、匹配工具及17个既有组件副本；没有下载安装。MixGo首次完整build在本地寄存器写入处失败：`write_reg`需要可写缓冲区，`const uint8_t`不匹配。`tools/build_cached_firmware.ps1`返回1，原始IDF返回2。修正单个局部缓冲区声明后，在原隔离目录同步源文件并刷新目的时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=mixgo-nova reconfigure build` 退出0，实际重新编译 `waveshare_spi_audio.cpp` 后链接成功。首次失败的日志和源码快照保留，没有删除或覆写为成功。

新板首次实际命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board mixgo-nova
```

三款旧入口在既有隔离目录同步本批代码，分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，初次与修正后的最终同步构建均退出0。初次日志实际包含main／audio／display重新编译；最后仅重新编译所选入口受影响的文件。VOX3和K08不编译被修正的SPI codec文件，所以最终可无应用对象重编，但完整源快照已同步且所选输入未变。不能用此结果宣称Magiclick分支已编译。

| Profile | 应用字节 | 最小应用分区 | 剩余字节 | 最终结果 |
| --- | ---: | ---: | ---: | --- |
| mixgo-nova | 2,241,072 | 2,752,512 | 511,440 | build退出0，8MB／QUAD及ES8374符号确认 |
| alientek-atk-dnesp32s3 | 2,239,520 | 3,145,728 | 906,208 | 旧ES8388共享音频路径回归退出0 |
| nulllab-ai-vox-v3 | 2,246,384 | 3,145,728 | 899,344 | 旧128fs ES8311路径回归退出0 |
| minsi-k08-dual | 2,207,248 | 3,145,728 | 938,480 | 旧数字音频路径回归退出0 |

四份镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目名／flash容量／checksum／validation hash均通过。`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认对应ES8374／ES8388／ES8311／digital_audio与dummy codec，MixGo的打开状态／增益／静音检查函数确实链接。生成配置明确QUAD／8MB／ES8374；其模型2,203,819字节，在2,359,296字节模型分区内，余155,477字节；其余模型分区为4MB。

逐份108项输入摘要、101个main文件集合、合并defaults、实际sdkconfig、main／audio／display编译宏、二进制板型身份、分区和模型容量均匹配。GC9107的24条命令与固定原表逐条数值比较一致，含两条14字节gamma；嵌套MIT文本与固定提交LICENSE匹配。原始失败／成功日志、初次／最终回归日志、镜像、ELF、编译命令、源快照及SHA-256在忽略目录 `evidence/mixgo-magiclick-builds-20261110/<profile>/`；四份轻量记录为 `cached_build_attempts_20261110_mixgo_magiclick_<profile>.json`。SDK原有改动、17组件override及既有LVGL弃用提示保留，不是干净发行依赖锁。

`python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`退出0。当前61官方变体有源码／127待源码，62个本地入口含1历史兼容入口；累计38官方变体曾各自缓存编译通过。当前4 passed／34 stale／6历史依赖预检失败／144 not_run（其中17项已有源码，含两款Magiclick缺依赖），全部188实机not_run。未运行测试套件或独立类型检查，未安装依赖、启动宿主服务、Git写入、上传、发布、刷机或OTA。全量适配目标仍进行中。

收尾实际执行 `git diff --check` 退出0；本批新增／未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，未发现空白错误（返回1为新增差异，仅有LF／CRLF提示）。与上批归档源快照逐文件diff，修改限于本批板级分支、布局选择及构建路由。四份记录的108输入与归档17／18项产物SHA-256再次与实际文件匹配；没有把旧缓存通过扩展为新板实机通过。
