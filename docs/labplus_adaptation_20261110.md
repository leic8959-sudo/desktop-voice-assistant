# 乐动掌控 V2 与掌控板 V3 适配（2026-11-10）

本批接入 `labplus-ledong-v2` 和 `labplus-mpython-v3` 两个官方发布变体。固定依据小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的config、实际板类、README及ES8388音频类，不执行上游代码。只有现有微雪1.8实物，两款新板无实机验收。全量清单仍为154目录／188变体。

## 显示、内存及按键

| 项目 | 乐动掌控 V2 | 掌控板 V3 |
| --- | --- | --- |
| 官方／本地profile | `labplus-ledong-v2` | `labplus-mpython-v3` |
| 应用身份 | `assistant_ledongv2` | `assistant_mpythonv3` |
| Flash／RAM | 16MB Flash，README明确外挂8MB PSRAM、QUAD模式 | 相同 |
| 屏幕 | JD9853 320×172，实际构造使用ST7789默认初始化 | 相同 |
| SPI | SPI2 mode0／20MHz，队列深度7，RGB16／big-endian | 相同 |
| 引脚 | MOSI37／CLK36／DC35／CS NC／RST NC／BL34 | MOSI37／CLK36／DC35／CS34／RST NC／BL33 |
| 背光 | 高有效，PWM初始关闭 | 相同极性，独立引脚 |
| 复位 | 先向STM8的7位地址0x11发单字节0x04，再panel reset、额外等待100ms | panel reset、额外等待100ms，不发送STM8命令 |
| 最终方向／窗口 | swapXY＋mirrorX＋mirrorY，gap0／34；面板和LVGL设置一致 | 相同 |
| UI／按键 | 复用320×172短屏布局与16px中文字体；BOOT0低有效，首次释放后接受按住说话／无触摸操作卡 | 相同 |

两个发布config没有自动覆盖PSRAM模式，但README明确要求QUAD；GPIO35–37又用作LCD，因此不能继承Octal默认值。板级defaults显式选择QUAD、16MB由注册表约束。生成配置需要单独核对，不能从JSON条目存在推断硬件容量已验证。

STM8复位只发送固定板类标注的LCD复位命令；add-device／单字节transmit／remove-device均检查返回值，通信超时100ms，失败向上返回。没有发送电机、摄像头或其他STM8控制命令。掌控板V3不创建该复位设备。

这里遵循实际板类的ST7789默认驱动路径，没有套用微雪Audio Board的32条JD9853专用表。微雪表的COLMOD05／gamma／最终mirrorY与这两板不同，仅凭同名屏控不能替换初始化序列。实际JD9853修订、默认表的颜色／复位／偏移效果仍待实物验证。字体显式加入16px中文分支；默认自定义字体行高31px，不能仅因布局复用就假定新板文字适配。

## 音频

两款均为ES8388，I2C0 SDA44／SCL43；I2S0 MCLK39／BCLK41／WS42／DIN40／DOUT38，24kHz输入／输出、256fs、初始stereo16；实际输入mono16／LEFT mask、输出mono16。两个handle共享一个codec，关闭时保持codec使能，生命周期由现有状态锁串行化。PA为NC，不能将任何其他GPIO推断为外部功放控制。

固定ES8388类默认输入参考false，输入增益24dB、模拟输出寄存器46–49设30（0dB）。本地复用既有I2S错误记录／显式超时收发、24dB待机与共用12dB播放降增益、模拟输出写后回读及实际增益／静音回调检查，沿用软件AEC。上游同时设ESP I2S和codec为主模式，本地使用现有ESP主／codec从配置，避免两端同时驱动时钟；实际时钟与声学仍待实测。

两板的UART0默认引脚43／44与I2C冲突，生成配置实际为USB Serial/JTAG主console、secondary none；不把UART0配置移植进来。

## 未完成边界

- README还列出B键46，但实际板类仅使用BOOT0，本批未接B46或额外触摸按键。
- LED GPIO1与README RGB16存在不同描述，本批均不实现；蜂鸣器、传感器、相机、休眠以及乐动V2电机功能未接入。
- 未实测PSRAM、面板、STM8 ACK／复位、背光、按键、ES8388输入槽／主从时钟／增益／静音／关闭重开、声学AEC、配网绑定及同板OTA。
- 源码／缓存构建只覆盖基础Wi-Fi语音终端，不把原厂全部教学或执行器功能列为完成。

## 构建与证据

两款新板各自完整缓存构建退出0。补齐短屏16px字体宏后，在各自原隔离目录同步 `display_ui.cpp` 并刷新目的时间，最终增量 `reconfigure build` 再次退出0。微雪Audio Board／MixGo公共回归初次和最终构建也均退出0；四份最终日志均包含 `display_ui.cpp` 实际重编，初次日志包含main／audio／display编译。旧日志保留，没有用初次未包含字体分支的镜像作最终交付。

实际新板命令（两次分别选择对应profile）：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board labplus-ledong-v2
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board labplus-mpython-v3
```

最终四板分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，只复用本机IDF5.5.6-dirty及17个既有组件副本，无安装。具体环境和日志在四份 `cached_build_attempts_20261110_labplus_<profile>.json`。

| Profile | 最终应用字节 | 应用分区字节 | 剩余字节 | 结果 |
| --- | ---: | ---: | ---: | --- |
| labplus-ledong-v2 | 2,242,080 | 3,145,728 | 903,648 | 完整及最终增量build退出0 |
| labplus-mpython-v3 | 2,241,840 | 3,145,728 | 903,888 | 完整及最终增量build退出0 |
| waveshare-audio-board | 2,256,096 | 3,145,728 | 889,632 | 旧专用JD9853／双codec回归退出0 |
| mixgo-nova | 2,241,072 | 2,752,512 | 511,440 | 旧8MB QUAD／ES8374回归退出0 |

每份实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目名／16或8MB容量／checksum／validation hash均通过。`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认两Labplus链接ES8388、实际增益／静音检查、16px中文字体，未链接31px自定义ui_font或微雪专用JD9853驱动；微雪仍链接原专用JD9853和双codec，MixGo仍链接ES8374。

四份各108项输入摘要、101个main文件集合、合并defaults、实际QUAD／Octal配置、codec配置、主／次console、四个main翻译单元的板级宏、二进制板型身份、应用和模型容量均匹配当前工作区。模型均2,203,819字节；两Labplus模型分区4MB，MixGo模型分区2,359,296字节。源快照、初次／最终日志、配置、依赖锁、镜像／ELF／编译命令和SHA-256归档于忽略目录 `evidence/labplus-builds-20261110/<profile>/`。SDK既有改动、无关BSP override和LVGL弃用告警仍保留，不把缓存构建当作干净发行依赖解析。

清单同步命令 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`退出0。当前63官方变体有源码／125待源码、64本地profile含1历史入口；累计40官方变体曾各自缓存编译通过。当前4 passed／36 stale／6历史依赖预检失败／142 not_run（其中17项已有源码），全部188实机not_run。

固定公开源代码、原始README及音频类归档于忽略目录 `evidence/labplus-source-20261110/`，URL与SHA-256见 `board_research_v2.5.2.json`。本批仅增加板级分支／构建路由／两个入口及defaults，无新驱动依赖。未安装依赖、启动宿主服务、运行测试套件或独立类型检查、Git写入、上传、发布、刷机或OTA；全量适配目标仍进行中。

收尾实际执行 `git diff --check` 退出0；本批未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。对照上一批源快照检查diff，修改限于板级路由／显示／音频与字体分支；四份108输入及17项归档产物摘要均再次匹配实际文件。所有构建进程已结束。
