# AiPi-Lite：原厂区域保留与128×128源码候选

2026-11-10，Asia/Shanghai。固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，范围仍为154个发布目录／188个变体。新增 `xorigin-aipi-lite`，应用身份 `assistant_aipilite`；本批只改本地源码和构建输入，没有读取原厂EUI／凭据或操作设备。

## 来源与必要分区差异

[固定AiPi-Lite目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/xorigin/aipi-lite)的config、实际板类、power_manager及README。其README明确提示原厂信息位于 `0x9000` 起16,384字节，包含恢复XOrigin／SenseCraft连接所需的EUI／凭据；原通用固件恰好将默认NVS放在同一区域。不能只保证刷写文件不覆盖它，应用启动后的NVS写入也必须隔离。

公开源码和逐文件URL／SHA256索引位于忽略目录 `evidence/aipi-public-20261110/`。没有导入原厂或上游驱动／应用实现，保留的是硬件事实和公开保留范围；未读取、复制、迁移或验证真实凭据，不新增第三方驱动源码许可或更改整体LICENSE。

板级专用 `boards/xorigin-aipi-lite/partitions.csv`：

| 分区 | 地址／大小 | 用途 |
| --- | --- | --- |
| nvsfactory | 0x9000／0x4000 | 自定义data subtype0x40，只读；只占位保留，应用不打开、不解析 |
| nvs | 0x10000／0x4000 | 小燕子独立默认NVS，用于自己的配网／绑定／偏好 |
| otadata | 0x14000／0x2000 | 本固件双OTA状态 |
| phy_init | 0x16000／0x1000 | PHY数据保留位置 |
| app0／app1 | 0x20000／0x320000，各0x300000 | 沿16MB基础布局的双OTA应用 |
| storage／model | 0x620000／0xC00000，大小0x5E0000／0x400000 | 与基础16MB布局相同 |

默认NVS初始化只会找到新的 `nvs` 标签／subtype，不会把raw只读factory分区当成可写NVS。AiPi启动先保持RTC GPIO10供电，再在调用 `nvs_flash_init()` 之前核分区元数据：factory地址／大小／只读、新NVS地址／大小／可写、OTAdata地址／大小必须匹配，否则记录错误并返回，不启动NVS、网络或绑定。用于拒绝只刷应用却仍使用旧分区表的布局；只检查分区表，不读取factory内容。GPIO10初始化有一次性状态，后续显示初始化不重复切供电。

原厂只公开这一段保留范围；布局和源码保护**不证明其他原始分区不会被重用，也不证明可以完整恢复原厂固件**。后续刷写仍需要明确授权、确切硬件修订、完整16MB备份及恢复方案，本轮没有刷写或执行恢复。16MB／Octal PSRAM沿固定根默认，非实物容量测量。

## 刷写工具的本地保护

`native_board_profiles.json` 为AiPi声明两个 `preserved_flash_ranges`：原厂0x9000／0x4000和新NVS0x10000／0x4000；其他板未声明时仍默认保护原NVS0x9000／0x4000。Python登记读取器核对非空有界列表、整数、容量、扇区对齐、排序且不重叠。

`tools/flash_native.py` 继续要求已有该板完整Flash备份及对应应用身份，仍不执行erase-all。写入前按4KB扇区向外取整核对每份产物的实际擦除范围，任何保留区相交即拒绝；解决非对齐产物本身未跨界、擦除扇区却跨界的缺口。输出记录保留具体保护范围。检查改变的是本地工具源码，没有调用刷写工具、没有创建真实设备备份，也没有运行模拟烧录测试。后续不得通过另一个全擦除命令绕过该保护并宣称原厂区仍保留。

## 终端引脚与界面

| 项目 | 本地源码候选 |
| --- | --- |
| 显示 | 128×128 ST7789、BGR565，SPI3 mode0／40MHz，CLK16／MOSI17／DC7／CS15／RST18，背光3高有效PWM；invert=false、swapXY=true／mirrorX=true／mirrorY=false、gap0 |
| 布局 | 复用已有128×128布局与16px中文、紧凑卡片／六位配对码；不缩放成240屏，真实裁切仍待实机 |
| Codec | 单ES8311输入／输出24kHz，I2C1 SDA5／SCL4，I2S0 MCLK6／BCLK14／WS12／DIN13／DOUT11，PA9高有效 |
| Clock | 实际板类构造 `use_mclk=false`，codec内部使用BCLK衍生时钟；I2S仍按来源输出MCLK6，真实时钟有效性待验收 |
| 输入／供电 | RTC10解除旧hold后置高并保持，BOOT42先释放再按住说话／卡片选择确认；没有虚构GPIO0或音量键，无触摸声明 |

config屏幕时钟宏写20MHz，但实际板类创建IO时明确40MHz，本地沿实际创建值并保留差异。其实际I2C初始化为端口1，codec旧参数却写端口0，本地保持I2C1 handle和端口一致，不新建一条端口0总线。

复用既有模拟codec增益、软件音量／静音、24→16kHz采样、唤醒与软件播放参考AEC。待机30dB／播放12dB及电压增益是现有候选配置，非此板声学校准。128像素上的BGR／swap／mirror、40MHz、无音量键操作、I2C1／PA9／BCLK时钟、音频静音重开、实际容量和同板OTA均未实机验收。

## 未完成能力

ADC1通道1电量／充电检测8、带充电条件的电源键1关机、自动省电／深睡／唤醒、LED46、原BOOT长按重新配网手势尚未实现。本地保持基本终端供电，不推断电池百分比、不套未标定阈值或在语音／OTA期间自动切电。只有现有微雪1.8实物，屏控修订未确认，全部188项实机状态仍为not_run。

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
  -Board xorigin-aipi-lite
```

使用本机已有SDK5.5.6-dirty／匹配工具／Python及17份本地override，Python外网守卫拒绝外网socket／DNS，未安装组件。SDK保留原NimBLE／idf_as_lib修改，override含其他BSP，不是干净发行依赖锁。

BOX0、Kevin SP V4和微雪LCD0.85分别在既有隔离目录使用临时 `xy_aipi_regression_20261110.ps1 -Stage <目录> -Board <profile>`，按内容摘要同步源码并刷新变化文件时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。增加AiPi启动分区检查后同步最终main／board power做增量收尾。微雪0.85最初两次配置退出1：C盘旧临时vendor多份源码缺失。失败日志保留，核版本及CMake／manifest后把17份override指向I盘已经准备好的同版本AiPi vendor，再在同一构建目录重试；无下载安装、删除或向C盘复制整套组件。本轮只观察到C盘剩约3.8GB与临时副本缺失，不推断缺失的原因。后续避免把回归依赖放在可被外部清理的临时目录。

AiPi完整缓存构建及最终同目录 `reconfigure build` 均退出0；最终增量前刷新隔离目录main／audio／display／power编译文件UTC时间，日志确认main／audio重编，包含NVS前的分区布局检查。BOX0／Kevin SP V4的最终增量回归均退出0，微雪0.85修复override后回归退出0；两次真实配置失败日志保留。既有LVGL9.6弃用接口警告仍在。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| xorigin-aipi-lite | 2,246,768 | 3,145,728 | 898,960（约29%） |
| alientek-atk-dnesp32s3-box0 | 2,245,760 | 3,145,728 | 899,968 |
| kevin-sp-v4-dev | 2,244,288 | 3,145,728 | 901,440 |
| waveshare-lcd-0.85 | 2,252,256 | 2,752,512 | 500,256 |

四款分别执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，项目名、0.85的8MB／其他16MB配置、checksum及validation hash有效。模型均2,203,819字节；0.85模型分区2,359,296字节，其他4,194,304字节，应用／模型均未超容量。AiPi用自己的分区表，0.85也按专用8MB表核容量。

一次性 `xy_record_aipi_20261110.py` 退出0：每份98个源码／配置摘要和完整91文件main集合、合并defaults、实际选择源、生成配置／容量一致；另记录两个主机工具的源码摘要。它直接解析AiPi生成的partition-table.bin，确认factory为type1／subtype0x40、readonly flag=2、offset0x9000／size0x4000，默认NVS为0x10000／0x4000且可写，OTAdata为0x14000／0x2000。读取flasher_args.json和产物大小后按4KB扇区核对五份写入区域，均不与factory或新NVS相交；没有生成写入factory／NVS的镜像条目。结果在 `aipi-partition-artifacts.json`。

这些是生成产物的只读检查，不调用flash_native、不创建模拟设备，不证明其运行错误分支或真实设备分区布局已经验收。记录为 `cached_build_attempts_20261110_aipi_<profile>.json`；日志、ELF／镜像、配置、源码和分区检查结果位于忽略目录 `evidence/aipi-builds-20261110/`。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前56个本地profile对应55个官方变体（另1历史兼容入口），133项尚无源码；累计34个官方变体曾分别通过缓存编译。按当前源码摘要，4项通过、30项历史通过证据过期、6项依赖预检失败、148项未构建（其中15项已有源码）。全部188项实机验收仍not_run。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/main.cpp firmware/assistant_espidf/main/audio_io.cpp tools/flash_native.py`，退出0；新增／未跟踪源码、配置、文档执行 `git diff --no-index --check -- NUL <文件>`，返回1表示新增差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件、独立类型检查、刷写模拟或设备验收；未安装依赖、启动宿主长期服务、刷机／OTA、Git写入、上传或发布。保留网关／运行时与自然插话的并发改动。
