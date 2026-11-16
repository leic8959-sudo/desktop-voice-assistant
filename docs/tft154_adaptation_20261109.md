# 常见TFT板源码与构建适配

最新星智阶段见[ABS2.0／NV3023方块记录](xingzhi_adaptation_20261109.md)：27个本地profile对应26个官方变体、162个待源码；以下保留本页对应阶段的数量和证据。

2026-11-09，Asia/Shanghai。继续小智v2.5.2全部154个发布目录／188个变体目标。本轮增加LC1.54、星智方块1.54、正辰1.54及面包板默认LCD四个实际源码入口，当前25个本地profile对应24个官方变体，另含1个历史兼容名；164个官方变体仍无源码。构建与实机状态另列，不把源码登记视为设备验收。

## 固定板型

| profile／应用身份 | 显示接线 | 音频／供电 |
| --- | --- | --- |
| `lc-s3-wifi-1.54tft`／`assistant_lctft154` | 240×240 RGB ST7789，mode0／40MHz；MOSI15、CLK14、DC10、CS13、RST16、BL21 | 单ES8311，24kHz输入输出；I2C SDA7／SCL6，MCLK5／BCLK4／WS2／输入3／输出1／PA8 |
| `nologo-xingzhi-cube-1.54tft-wifi`／`assistant_xztft154` | 240×240 RGB ST7789，mode3／80MHz；MOSI10、CLK9、DC8、CS14、RST18、BL13 | 数字I2S输入16kHz、输出24kHz；MIC WS4／BCLK5／DATA6，speaker BCLK15／WS16／DATA7；RTC GPIO21高为初始供电状态 |
| `zhengchen-1.54tft-wifi`／`assistant_zctft154` | 240×240 RGB ST7789，mode3／80MHz；MOSI41、CLK42、DC40、CS21、RST45、BL20 | 相同数字音频引脚／16kHz输入和24kHz输出；RTC GPIO2高为初始供电状态 |
| `bread-compact-wifi-lcd`／`assistant_breadlcd` | 官方发布配置明确ST7789 240×320，mode0／40MHz；MOSI47、CLK21、DC40、CS41、RST45、BL42 | 同数字音频引脚／16kHz输入和24kHz输出，无此发布配置对应的电源保持输出写入 |

四个入口都无触摸，GPIO0为BOOT／手动输入；屏幕反色开启、RGB顺序、无镜像／交换轴、gap0。根页按住说话，卡片页短按选择／长按松手确认；长文字自动滚动，操作按钮固定。三个1.54入口使用240×240布局，面包板另用240×320竖屏布局，没有为相近尺寸直接套另一块板的引脚。

固定提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的config.h、config.json、板类和power_manager.h联合读取。官方全局sdkconfig.defaults明确16MB Flash、S3默认Octal PSRAM，四个发布配置没有容量覆盖，因此本轮候选按该编译基线登记。**这是固件要求，未测量任意购买实物或DIY模组的真实容量。** 不宣称所有ESP32-S3／面包板屏幕可用同一镜像。

- [LC固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/lc-s3-wifi-1.54tft)
- [星智固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-cube-1.54tft-wifi)
- [正辰固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/zhengchen/1.54tft-wifi)
- [面包板固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/bread-compact-wifi-lcd)
- [全局容量默认](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/sdkconfig.defaults)
- [S3默认](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/sdkconfig.defaults.esp32s3)

面包板config.json显式选择`CONFIG_LCD_ST7789_240X320=y`，全局Kconfig也默认该项，不能误称其发布屏幕未指定。本轮只接入这个确切默认；其ST7735／ILI9341／其他ST7789尺寸及自定义屏幕仍需独立配置和适配，不借一个官方发布名冒称全部显示组合已覆盖。

## 复用与最小源码修改

`tft_board.h`集中四板常量，未识别的板型编译报错。原SPI显示实现共用，针脚命名空间从品牌名调整为`spi_board`，已有微雪SPI显示／音频同步使用它，不增加第二套ST7789驱动。数字板没有I2C codec，不创建假的I2C总线或触摸设备；LC先初始化实际I2C总线后复用单ES8311输入输出句柄。

数字板复用`digital_audio.cpp`：I2S0采集、I2S1输出，16kHz左槽输入／24kHz左槽输出，32位承载。麦克风右移12位并饱和到PCM16，扬声器PCM16乘65536后分块发送；保留短读／短写检查与真正的PCM静音。16kHz采集在公共前端直通，不添加不存在的FIR延迟；播放参考仍24→16kHz。该路径此前只有未编译的1.46／1.85源码实例，本轮两款1.54数字板和面包板会提供各自编译证据，仍不推断声学效果或SPD2010／ST77916分支已验证。

星智／正辰只复用各自确切的供电高电平，引脚21／2在初始化时先释放旧RTC保持再设输出高，不把充电检测GPIO38／9当电源输出。LC的GPIO38同样只属于未实现的充电检测输入。自动深睡、专用音量键、LED和电池／温度遥测未接入；未照抄正辰源代码把电量最低显示20%的行为。

独立profile、应用名、manifest及默认配置均纳入原登记表、构建／烧录／OTA身份校验，没有伪造依赖锁。没有复制上游应用逻辑、计时器、电池估算或工具注册代码，使用既有IDF ST7789、esp_codec_dev、LVGL和SR组件，不安装新依赖。

## 实际构建命令与执行边界

四个profile分别运行已有包装入口，参数除`-Board`外相同：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board lc-s3-wifi-1.54tft
# 另三次分别为nologo-xingzhi-cube-1.54tft-wifi、zhengchen-1.54tft-wifi、bread-compact-wifi-lcd
```

四份独立目录中使用已安装IDF5.5.6-dirty和17个既有组件源override，Python网络守卫拒绝外部连接／解析。六个此前已编译入口另做公共源码回归，各自在原隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`。实际退出码／镜像尺寸／最终输入摘要在下方记录。

一份旧T-Circle V1.1回归先在kconfiglib读取help文本时出现`OSError: [Errno 9] Bad file descriptor`，完整堆栈保留，尚未把它当作固件源码错误或套用旧成功记录；确认该进程已退出后才会在同目录重试。其他已确认运行的进程持续等待，不因观察超时而重新启动。

原有GENJU TECH1.54仍为inventory_only：它使用SparkBotEs8311子类、输入输出16kHz，且重写EnableOutput并注释显示／PA冲突，需要核清对应时钟及PA生命周期，不能只替换LC的名字和引脚。

牛牛只有现有微雪1.8实物；新板的Flash／PSRAM、屏幕方向／色彩、BOOT录音／卡片、供电、MIC槽位／增益、功放／软件静音、回声与唤醒、配对／提醒及同板OTA均待实机。当前任务没有安装、测试套件、独立类型检查、长期服务启动、Git写入、上传、发布、刷机或OTA。

## 最终构建与镜像证据

四个新增profile的完整缓存构建均退出0，随后各做最终源码构建，退出0；旧六个profile在保留失败日志后也以最终源码构建退出0。实际命令是前述包装器、各profile的`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`或相同参数`build`，确切调用和退出码逐份记在JSON，不把所有阶段合成一次成功。

十个实际应用各执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用名、8／16MB镜像设置与对应profile一致，checksum和validation hash有效。无串口连接。

| profile | 最终应用字节 | 实际应用分区剩余 |
| --- | --- | --- |
| lc-s3-wifi-1.54tft | 2,531,088 | 614,640 |
| nologo-xingzhi-cube-1.54tft-wifi | 2,493,120 | 652,608 |
| zhengchen-1.54tft-wifi | 2,493,104 | 652,624 |
| bread-compact-wifi-lcd | 2,492,192 | 653,536 |
| waveshare-lcd-0.85 | 2,538,400 | 214,112（单应用0x2A0000） |
| lilygo-t-circle-s3 | 2,507,408 | 638,320 |
| lilygo-t-circle-s3-v11 | 2,510,400 | 635,328 |
| waveshare-amoled-1.8-v2 | 2,614,816 | 530,912 |
| waveshare-lcd-1.54 | 2,546,112 | 599,616 |
| waveshare-lcd-1.83 | 2,615,552 | 530,176 |

除0.85外应用分区均为0x300000，模型2,203,819字节也在各自实际模型分区内。50份固件源码／配置／许可摘要逐份比对工作区相同；0.85比对其板级8MB分区，其他入口比对自己的实际分区输入。新三款数字TFT实例实际编译16kHz直通采集及32位PCM转换；LC实际编译单ES8311路径。只能据此证明编译／链接／镜像结果，不能证明采样电平、声学AEC或未知电气修订。

`firmware/assistant_espidf/cached_build_attempts_20261109_tft154_<profile>.json`记录实际命令、SDK、50份输入、产物／日志摘要。原始成功／失败日志、镜像、ELF、模型、分区、配置、输入快照和执行脚本保存在Git忽略的`evidence/tft154-builds-20261109/`，未复制到网关OTA路径。

失败原样保留：三份旧微雪SPI入口因中性命名空间重构遗漏board_profile尺寸引用导致编译失败，修正后重建；旧T-Circle V1.1在kconfiglib读取help时`OSError: Bad file descriptor`，确认进程退出后同目录重试通过。后续最终日志和错误阶段各自保存，未覆盖成全部首轮通过。

SDK只读复核HEAD为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，已有NimBLE及idf_as_lib修改保留，镜像自报5.5.6-dirty。现有LVGL flag API弃用告警仍在；最终新数字音频／TFT头／供电／SPI显示源码编译未新增对应告警。17个本地override含无关旧BSP，不能称为干净发行锁。

当前24个官方变体有源码，其中10个有当前源码缓存构建证据、14个已有源码未编译，164个待源码；188项本轮实机全部未执行。只有现有微雪1.8实物，其他板需取得后逐项验收。所有本轮构建进程已退出；没有安装依赖、运行测试套件／独立类型检查、启动长期服务、Git写入、上传、发布、刷机或OTA。全量目标仍进行中。

最终编辑审查执行`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp`退出0；36份相关未跟踪文件分别执行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。矩阵确认10个passed_cached_sources均有当前输入摘要匹配。运行时、网关、control_protocol及自然插话并发工作未编辑、覆盖或回滚。
