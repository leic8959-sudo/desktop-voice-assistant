# 敏思 K08 与 NULLLAB AI-VOX3 源码适配（2026-11-10）

本批为两个官方发布变体增加基础 Wi-Fi 语音终端源码入口。固定参照小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的实际 config、板类和音频类，未执行上游源码。只有现有微雪1.8实物，两款新板未实机验收。全量清单仍为154目录／188发布变体。

## 固定硬件事实与实现

| 项目 | 敏思 K08 | NULLLAB AI-VOX3 |
| --- | --- | --- |
| 本地／官方 profile | `minsi-k08-dual` | `nulllab-ai-vox-v3` |
| 应用身份 | `assistant_minsik08` | `assistant_aivox3` |
| 芯片与存储 | ESP32S3N16R8，16MB flash／Octal PSRAM | ESP32-S3R8／16MB flash／Octal PSRAM |
| 显示 | ST7789 240×240，RGB，反色，SPI3 mode3／40MHz | 可选外挂ST7789 240×240，RGB，反色，SPI3 mode0／40MHz |
| 显示引脚 | MOSI17／CLK8／DC15／RST16／CS NC／BL7高有效 | MOSI21／CLK17／DC14／CS15／RST NC／BL16高有效 |
| 最终方向／窗口 | swapXY＋mirrorY，gap80／0，同时设置面板和LVGL方向 | 不交换不镜像，gap0／0 |
| 录音 | INMP441，WS9／BCLK10／DIN18，16kHz，LEFT32bit转PCM16 | ES8311，24kHz，初始stereo16，打开为mono16，37.5dB待机增益 |
| 播放 | MAX98357，BCLK5／WS6／DOUT4，24kHz，LEFT32bit | ES8311＋NS4150B，24kHz，PA NC，5V／3.3V硬件增益参数 |
| 音频总线 | 独立I2S：本地麦克风I2S0／扬声器I2S1，均主模式 | I2C0 SDA13／SCL12；I2S0 MCLK11／BCLK10／WS8／DOUT7／DIN9 |
| 时钟 | 不用MCLK | I2S初始化、codec mclk_div、每次sample open均为128fs |
| 按键 | BOOT0／音量加47／减46，低有效 | BOOT0／音量加46／减45，低有效 |

复用现有240×240布局、按住说话、无触摸操作卡、对话音量键、输出静音、网络PCM24和软件AEC。三键首次全释放后才接受输入，同时按键返回Invalid，由共用输入任务取消当前操作。VOX3使用同一双向codec handle，关闭后保持codec使能；播放期间仍用既有12dB输入增益，软件参考沿用共用时间调度，没有引入上游参考缓冲。128fs与37.5dB只在VOX3宏内生效。两板沿用小燕子的按住说话／操作卡／对话音量交互，原厂点击切换聊天、网络切换及长按最大音量等手势不视为已移植。

K08上游 `LAMP_GPIO18` 与麦克风DIN18冲突，实际板类还实例化灯控示例。本地仅把18作为音频输入，不创建灯控工具。上游RTC21供电代码是注释，不能据此主动配置电源输出。ADC1和充电3保留；VOX3的ADC18和充电47保留。VOX3原厂发布配置选择主console-none，本地板级defaults同样选择。实际IDF5.5.6配置仍启用secondary USB Serial/JTAG，不能据此声称所有USB调试输出均关闭。

本批只增加六个现有main文件内的板级分支、两套板级清单／defaults、统一注册表、研究清单和本文档；不新增面板或音频驱动依赖，不改通用音频算法／服务协议／无屏绑定决策。没有复制上游实现函数。固定公开源码与逐文件摘要归档于忽略目录 `evidence/k08-aivox3-source-20261110/`，URL及SHA-256在 `board_research_v2.5.2.json`。

## 尚未完成

- 两板蜂窝ML307／网络切换、电量／充电显示、低电压处理、休眠、指示灯未实现。
- VOX3的OLED／SD／电机与扩展板未实现。本入口要求实际装有该ST7789屏；无屏或OLED实物应另建配置。
- K08屏幕80／0偏移、16kHz录音槽位、24kHz播放与静音；VOX3的128fs主时钟、单声道时钟重配、增益、关闭重开；以及两板供电、按钮、中文界面、联网绑定、声学AEC和同板OTA均待实机验收。
- 不把基础Wi-Fi构建结果当作DUAL网络或整机所有外设已适配。

## 构建与检查

实际复用本机IDF5.5.6-dirty、匹配工具和17个既有组件副本，没有安装依赖。新板各自完整构建、两款旧板的公共代码回归均退出0。

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board minsi-k08-dual
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board nulllab-ai-vox-v3
```

旧板分别在已有隔离目录同步变化main输入并刷新目的时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，板型为 `xorigin-aipi-lite` 与 `du-chatx`。原始命令和环境见四份 `cached_build_attempts_20261110_k08_aivox3_<profile>.json`。每份日志实际包含main／audio／display重新编译；新板使用全新stage完整构建，没有复用旧bin。

| Profile | 应用字节 | 3MB分区剩余字节 | 结果 |
| --- | ---: | ---: | --- |
| minsi-k08-dual | 2,207,248 | 938,480 | 完整build退出0；镜像身份／校验通过 |
| nulllab-ai-vox-v3 | 2,246,384 | 899,344 | 完整build退出0；镜像身份／校验通过 |
| xorigin-aipi-lite | 2,246,784 | 898,944 | 公共单ES8311路径回归退出0；AiPi专用分区保留 |
| du-chatx | 2,207,152 | 938,576 | 公共数字音频路径回归退出0；镜像身份／校验通过 |

四份产物均实际执行只读 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目名／16MB flash／checksum／validation hash通过。`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf` 确认数字板的digital_audio／dummy codec符号和单codec板的ES8311符号。逐份核对105项输入摘要、98个main文件集合、合并defaults、实际sdkconfig、main／audio／display编译宏、二进制内板型身份、分区和模型容量，均匹配。模型文件2,203,819字节，位于4MB模型分区内。

原始日志、镜像、ELF、配置、依赖锁、编译命令、源快照及摘要位于忽略目录 `evidence/k08-aivox3-builds-20261110/<profile>/`；固定源代码证据另存。SDK仍有原先本地改动，17组件override也含无关BSP，不能当作干净发行依赖锁；既有LVGL弃用告警仍在。实际secondary USB/JTAG状态见上文。

最终矩阵同步命令 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前58个官方变体有源码／130个待源码，59个本地profile含1历史入口；累计37项官方变体各自曾缓存编译通过。当前4项passed、33项历史通过stale、6项依赖预检失败、145项not_run（其中15项已有源码），全部188项实机not_run。

未运行测试套件或独立类型检查，未启动宿主服务、安装依赖、Git写入、上传、发布、刷机或OTA。`git diff --check`退出0；本批新增／未跟踪文件用`git diff --no-index --check -- NUL <file>`检查，无空白错误（返回1表示新增差异），仅有LF／CRLF提示。与上一批源快照做逐文件diff，六个main文件差异均限于本批板型分支；本批仅为基础Wi-Fi源码和缓存构建交付，全量目标仍进行中。


2026-11-10后续时钟更正：T-CameraPlus批次读取MAX98357A/B规格第19页，明确24kHz及11025Hz均不支持。敏思K08的已声明MAX98357和Guition外接MAX98357A入口改为16kHz物理播放，复用既有网络24→16kHz FIR及相同实际PCM软件AEC参考；Guition原板NS4168入口保留原24kHz。敏思新缓存回归及证据见[本批记录](tcamera_adaptation_20261110.md)；Guition外接入口仍缺GT911未编译，均未新增实机验收。本文前段24kHz输出为历史状态，当前以profile源码为准。
