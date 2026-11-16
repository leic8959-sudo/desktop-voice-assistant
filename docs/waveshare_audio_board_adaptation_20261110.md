# 微雪ESP32-S3 Audio Board默认JD9853源码候选

2026-11-10，Asia/Shanghai。固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，范围仍为154个发布目录／188个变体。新增本地profile `waveshare-audio-board`，应用身份 `assistant_wsaudio`，映射官方 `waveshare-esp32-s3-audio-board`。该板默认带JD9853屏幕，不能用已有Doit无屏入口替代。

## 固定来源及配置范围

来源为[固定Audio Board目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-audio-board)的config、板类、JD9853驱动／头及README，同提交Kconfig默认选择 `AUDIO_BOARD_LCD_JD9853`。另核对 `main/display/lcd_display.cc` 的实际LVGL注册行为。公开副本、命令提取索引与逐文件URL／SHA256位于忽略目录 `evidence/audio_board-public-20261110/`。

本轮对厂家Wiki的公开请求在30秒后超时，没有读取成功，不作为厂家引脚／面板证据。固定小智来源足以建立源码候选，不能据此声称实际硬件已确认。可选ST7789 240×320配置并非独立官方发布变体，本批没有实现或继承构建结果；相机／触摸／LED也仍未完成。

## 面板数据与本地驱动

板内JD9853驱动的有效默认表含32条命令，最大32字节gamma数据；保留重复DF写入、COLMOD0x05、地址窗口及120ms表内延时，未启用注释里的其他控制器表。字面数据长度逐项核对，显式长度0的条目不发送其占位字节。初始化前缀沿实际驱动：sleep-out／100ms、MADCTL0x00、COLMOD0x55，之后再执行默认表；软件RESET后20ms也保留。

`jd9853_panel_commands.h` 保留来源Espressif2022–2023／Apache-2.0通知，条款和改编范围位于 `main/third_party/xiaozhi_jd9853/`。没有整份导入上游应用板类，未改变整体项目许可，亦不把来源许可当作硬件验收或完整发行审计。

本地 `jd9853_panel.cpp` 提供RGB565专用生命周期：检查reset／init／方向／窗口／像素传输错误，正确发送矩形字节数，并检查逻辑172×320／旋转后320×172可见范围和偏移后的物理240×320显存范围。来源draw函数丢弃tx_color返回值，本地返回实际错误；MADCTL写入成功后才更新缓存，避免接口失败却宣称方向已切换。没有让通用ST7789的额外厂商命令混入JD9853序列。

方向需看最终效果：板类先调用mirrorX=true／swapXY=true，但随后 `SpiLcdDisplay` 注册LVGL时会应用config的swapXY=true／mirrorX=false／mirrorY=true；本地统一使用后者，MADCTL为0xA0。板类set_gap(0,34)保留在panel内部，LVGL传入offset0只是不额外加显示偏移，没有把gap清零。本地保持gap(0,34)，避免把屏幕位置依据猜成0或按初始mirrorX值覆盖最终方向。真实方向／颜色／34像素窗口仍须实机验收。

## 引脚、扩展口与终端行为

| 项目 | 本地源码候选 |
| --- | --- |
| 存储 | 16MB Flash／Octal PSRAM沿固定默认，当前config.json未覆盖；非实测容量 |
| 屏幕总线 | SPI2 mode0／20MHz，CLK4／MOSI9／DC7／CS3，GPIO复位NC，背光5高有效PWM；config有MISO8，但实际初始化未使用，本地也不驱动 |
| 面板／布局 | JD9853，RGB565、COLMOD0x05、invert=true、swapXY／mirrorY、gap0／34，320×172独立布局、16px中文、固定卡片按钮和配对码；页脚留在172像素高度内 |
| 控制总线 | I2C0 SDA11／SCL10，TCA9555 7bit地址0x20；不套8bit TCA9554寄存器或地址 |
| Codec | ES8311输出＋ES7210输入24kHz，I2S0 MCLK12／BCLK13／WS14／DIN15／DOUT16；ADC默认8bit地址0x80，PA GPIO=NC |
| 采集／输入 | 4×16bit TDM采集、slot0／MIC1单声道，TX初始32bit双槽匹配64时钟／帧；BOOT0先释放再按住说话／卡片短按选择及长按松手确认，无虚构触摸或音量键 |

`waveshare_audio_board.cpp` 只拥有TCA9555 reset0／1和PA8：启动先置PA低、reset高，再配置所拥有方向，按固定板类执行高10ms／低10ms／高复位流程；写后回读、I2C调用与锁等待100ms超时。两枚reset在来源注释称LCD／TouchPad，具体哪一位对应屏幕或可选触摸仍待硬件核对；按成对来源时序实现，不据此声称触摸功能已经接通。

相机reset／供电位5／6的输出和方向、其他扩展口位保持原值，不复制上游在初始化阶段自动拉起摄像头的行为。复位在单线程启动阶段完成，后续功放读改写有互斥，避免覆盖同端口的其他电平。读改写读取输出寄存器，不拿输入电平代替输出锁存。实际TCA9555芯片、复位和PA有效电平需要实物验收。

双codec复用现有显式传输超时、真实set_fmt／enable状态检查、物理MIC1增益及软件音量。待机30dB／播放12dB沿既有候选策略，非此板声学校准。硬件参考通道尚未用于本地AFE，仍使用软件播放参考；TDM4选择不等于硬件AEC／波束成形已验收。

PA8高有效，启动关闭，静音／中止／close先尝试关PA，再静音DAC；只有实际DAC mute回调成功后才允许启用PA，回读失败再尝试关闭并返回原错误。沿既有板级扩展口音频处理检查实际DAC／gain回调，避免codec_dev1.5.11丢弃这些返回值；其他未接管codec底层错误仍待核验。I2C失败时物理状态可能未知，不能把重试当已静音。

## 未完成能力和实机边界

只有现有微雪1.8实物，屏控修订尚未确认。JD9853实际屏控／gamma／字节序／偏移／方向／20MHz、短屏文字／按钮／配对码、TCA9555 reset／PA／故障恢复、ES8311／ES7210时钟／MIC1槽与增益／静音重开／唤醒／AEC、真实容量及同板OTA均未验收。可选ST7789、触摸、摄像头、LED38及原BOOT点击配网手势仍未完成，不把默认入口说成所有可选硬件就绪。全部188项实机状态仍not_run。

## 实际构建

新板实际调用：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board waveshare-audio-board
```

使用已有SDK5.5.6-dirty／匹配工具／Python及17份本地override，Python外网守卫拒绝外网socket／DNS，未安装组件。SDK保留原NimBLE／idf_as_lib修改，override含其他BSP，不是干净发行依赖锁。BOX3、立创和AiPi分别在I盘已有隔离目录运行临时 `xy_audio_board_regression_20261110.ps1 -Stage <目录> -Board <profile>`，按内容摘要同步变化并刷新文件时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖现有双codec／AW9523B／PCA9557／128像素单codec界面与共用音频生命周期。

新板完整缓存构建和最终同目录 `reconfigure build` 均退出0；最终增量前刷新main／audio／SPI audio／SPI display／JD panel／expander／display UI编译文件UTC时间，日志确认main、audio、JD panel重新编译，纳入页脚上移4像素的收尾调整。三个旧入口回归均退出0，实际main／audio重编日志已核对。页脚差异只在WS_AUDIO分支，已同步旧入口完整快照，未为未选中分支重复编译，也不据此声称旧入口编译了JD9853。既有LVGL9.6弃用接口警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| waveshare-audio-board | 2,256,096 | 3,145,728 | 889,632（约28%） |
| alientek-atk-dnesp32s3-box3 | 2,250,384 | 3,145,728 | 895,344 |
| lckfb-lichuang-dev | 2,263,424 | 3,145,728 | 882,304 |
| xorigin-aipi-lite | 2,246,768 | 3,145,728 | 898,960 |

四款分别实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份、16MB配置、checksum及validation hash有效。模型均2,203,819字节、分区4,194,304字节，应用与模型均未超容量。新板另实际执行匹配工具链 `xtensa-esp32s3-elf-nm.exe -C build/assistant_wsaudio.elf`，退出0，最终ELF保留 `waveshare_audio_board_init`、`jd9853_panel_create`、`es8311_codec_new`、`es7210_codec_new`。这只证明所选代码进入产物，不能证明面板、复位、时钟或声音工作。

一次性 `xy_record_audio_board_20261110.py` 退出0，核对每份105个源码／配置摘要和完整98文件main集合、合并defaults、实际选择源、生成配置、模型／应用容量及新板链接符号，并要求最终main／audio重编日志。记录为 `cached_build_attempts_20261110_audio_board_<profile>.json`，日志、ELF／镜像、配置与源码副本位于忽略目录 `evidence/audio-board-builds-20261110/`。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前57个本地profile对应56个官方变体（另1历史兼容入口），132项尚无源码；累计35个官方变体曾分别缓存编译通过。按当前源码摘要，4项通过、31项历史通过证据过期、6项依赖预检失败、147项未构建（其中15项已有源码）。全部188项实机验收仍为not_run。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0；新增／未跟踪源码、许可、配置、文档以 `git diff --no-index --check -- NUL <文件>` 空白审阅，返回1表示新增差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、刷机／OTA、连接硬件验收、Git写入、上传或发布；网关／运行时与自然插话的并发改动保留。
