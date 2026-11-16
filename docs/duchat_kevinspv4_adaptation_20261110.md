# DuChatX 与 Kevin SP V4 板级源码候选

2026-11-10，Asia/Shanghai。继续固定小智 v2.5.2 提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个发布变体。新增 `du-chatx` 和 `kevin-sp-v4-dev` 的基础语音终端源码，独立应用身份、板级依赖清单与采样率；相机、蜂窝、板卡专属电池／休眠／LED能力尚未实现，不宣称整板交付或实机可用。

## 固定来源

- [DuChatX](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/du-chatx)：config.h、config.json、du-chatx-wifi.cc、power_manager.h；实际构造器使用 ST7789 和 NoAudioCodecSimplex。
- [Kevin SP V4](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/kevin/sp-v4-dev)：config.h、config.json、kevin-sp-v4_board.cc；实际收放音均16kHz，屏幕引脚写在板类中。
- 同提交 no_audio_codec.cc／.h、es8311_audio_codec.cc 和 sdkconfig.defaults／sdkconfig.defaults.esp32s3：核对数字音频32bit左槽、输入右移12位、codec默认MCLK、16MB Flash／Octal PSRAM默认。两款config.json没有覆盖存储默认，仍需实物核容量，不能把构建配置当芯片测量。

公开来源副本与逐文件URL／SHA256索引在忽略目录 `evidence/duchat-public-20261110/`。最初Python urllib读取遭遇连接重置，没有保存成功内容；后改用带30秒超时的 `Invoke-WebRequest` 读取固定URL，成功保存。没有执行上游代码或下载安装组件，没有搬入上游驱动表／电源类／相机代码，不新增第三方驱动源码。

## DuChatX

profile=`du-chatx`，应用身份=`assistant_duchatx`。

| 项目 | 本地源码候选 |
| --- | --- |
| 屏幕 | 128×160矩形、固定板类实际ST7789构造；RGB565、SPI3 mode0／40MHz，CLK17／MOSI18／DC8／CS16／RST20／背光9 |
| 显示方向 | swapXY=false、mirrorX/Y=true、invert=false、gap=(2,1)；LVGL注册保持相同镜像，不把方向覆盖回默认 |
| 输入 | 独立数字I2S麦克风16kHz，BCLK38／WS39／DIN7，LEFT 32bit槽；本地I2S0采集、右移12位饱和转PCM16，沿原数字音频实现 |
| 输出 | 24kHz，BCLK42／WS2／DOUT40，独立I2S1、LEFT 32bit槽，PCM16扩到32bit；软件音量、PCM清零静音，PA=NC |
| 电源 | RTC GPIO1先解除旧hold再置高，在显示初始化之前保持供电；不对其他板电源引脚套用同值 |
| 按键 | BOOT0先释放再按住说话；卡片使用现有短按选择、长按松手确认。没有虚构音量键／触摸 |

数字输入没有模拟增益API，接现有16kHz AFE／唤醒与软件AEC；数字输入／输出端口本地沿已有I2S0输入／I2S1输出分配，上游Simplex的端口顺序相反，但各自时钟与引脚独立。麦克风具体型号、槽选、输入增益与时钟仍待实测。

128×160使用独立布局：112像素文字宽度、16px中文、128×56点云画布、固定卡片动作区和独立配对遮罩。沿现有小屏滚动文本／简短标签／六位码呈现，不拉伸成128×128或160×160。没有真实面板或浏览器渲染验收证据。

ADC／充电GPIO6、电量估算、LED48、自动睡眠／关机／唤醒及原BOOT点击配网手势尚未接入。本地保持常开语音终端供电，不输出虚假电量。

## Kevin SP V4

profile=`kevin-sp-v4-dev`，应用身份=`assistant_kevinspv4`。

| 项目 | 本地源码候选 |
| --- | --- |
| 屏幕 | 240×280 ST7789、RGB565、SPI3 mode3／80MHz，CLK21／MOSI47／DC45／CS14／RST=NC／背光48 |
| 显示方向 | swapXY=false、mirrorX/Y=false、invert=true、gap=(0,20)；没有可驱动的物理复位脚 |
| Codec | 单ES8311，输入／输出均16kHz；I2C1 SDA4／SCL5，I2S0 MCLK42／BCLK46／WS1／DIN2／DOUT3／PA41，MCLK作为codec时钟 |
| 音频路径 | 复用已支持16kHz单codec的初始化、模拟增益、软件音量／AEC、静音和关闭重开逻辑，不伪装为24kHz或独立数字麦克风 |
| 输入 | BOOT0先释放再按住说话，卡片沿既有单键确认流程；无触摸或物理音量键 |

上游实际 `InitializeCodecI2c` 创建I2C1，却将I2C_NUM_0旧参数连同I2C1 handle传给codec；本地按实际总线初始化统一使用I2C1 handle及端口，不据旧参数再创建一条总线。该修正的物理效果仍需实机核对。

240×280使用独立布局，保留16px中文、原尺寸卡片按钮、配对码和更多正文高度。屏幕80MHz信号质量、20行偏移、无物理复位的上电序列均未实测。

相机、ML307及LED38尚未实现。固定源码的相机数据脚与ML307 UART12／13重叠，不能同时盲目启用；当前应用不初始化这些外设。ES8311硬件增益电压沿既有候选值，播放期间12dB／待机30dB是现有软件AEC策略，不能据此宣称已完成此板声学校准。

## 继续调查的板型

这些只有固定源码调查，没有登记native profile或继承本批构建结果，详见 `board_research_v2.5.2.json`：

| 板型 | 已证实的实施差异 |
| --- | --- |
| Jiuchuan S3 | 240×296、BGR、SPI3 mode3／80MHz，文件叫gc9301而实际创建gc9309na；另有RTC电源5／高有效电源键3、I2C1 ES8311和独立电源管理。需核驱动许可、初始化及实际面板身份 |
| RYMCU BigSmart | 320×240 ST7789、SPI3 mode2、低有效背光、ES8311＋ES7210、GT911；PCA9557 0x19管LCD及PA，不能替换成直接GPIO。相机和电池功能另记 |
| 虫洞BOX／BOX LCD35 | 2.8英寸配置ST7789、3.5英寸配置ST7796，实际共用ST7789创建函数，需核实；FT5x06／GT911探测、方向持久化／坐标修正、双codec及蜂窝／相机不能只套240屏分支 |
| 微雪S3 Audio Board | 带屏，Kconfig默认JD9853 320×172，另可选ST7789；16bit TCA9555负责屏复位和PA等，已安装8bit TCA9554不能替代；双codec及JD9853偏移／方向需要独立处理 |

## 实际构建与剩余验收

本批完整缓存构建入口，分别以两个profile运行：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board <du-chatx或kevin-sp-v4-dev>
```

使用已有SDK5.5.6-dirty及17份既有组件override，外网守卫拒绝Python外网socket／DNS。SDK保留原NimBLE／idf_as_lib改动，override包含非选中BSP；不是干净发行依赖锁。

微雪LCD0.85的首次回归在CMake配置阶段退出1，旧隔离目录中缺少9份清单仍引用的vendor副本。保留 `duchat-regression-initial.log`，逐项核版本后先从本机现有组件缓存补齐缺失副本；第二次配置仍退出1，原有ES8311组件目录也缺少CMakeLists，说明部分旧目录内部不完整，保留 `duchat-regression-restore-initial.log`。随后恢复全部17份本机同版本组件副本并核CMakeLists／manifest存在，在同一目录重试；没有下载安装、删除或修改工作区组件。此回归因此重新编译完整依赖，不能称为纯增量构建。微雪1.8-v2使用其原有独立隔离目录回归。临时 `xy_duchat_regression_20261110.ps1 -Stage <目录> -Board <profile>` 同步完整main／third_party和根／板级配置后运行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。

两款新板的完整 `build_cached_firmware.ps1` 构建均退出0，随后同目录最终 `reconfigure build` 均退出0；微雪LCD0.85恢复组件后的回归及1.8-v2回归均退出0。新板最终配置与完整构建相同，没有为了改记录重编无变化源码。编译日志仍有既有LVGL9.6弃用API警告，不为此修改无关显示实现。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| du-chatx | 2,207,152 | 3,145,728 | 938,576（约30%） |
| kevin-sp-v4-dev | 2,244,288 | 3,145,728 | 901,440（约29%） |
| waveshare-lcd-0.85 | 2,252,112 | 2,752,512 | 500,400（约18%） |
| waveshare-amoled-1.8-v2 | 2,615,648 | 3,145,728 | 530,080（约17%） |

四份实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0。项目名、0.85的8MB／其他三款16MB设置、checksum及validation hash有效。0.85采用自己的8MB分区表，不能拿根16MB表核余量。模型均2,203,819字节；0.85模型分区2,359,296字节，剩155,477字节，其余三款为4,194,304字节。

一次性归档脚本 `xy_record_duchat_20261110.py` 退出0：逐项核对77个源码／配置摘要、完整70文件main集合、合并defaults、实际选择的组件源、生成配置和应用／模型容量。DuChatX确实选择 `digital_audio.cpp`，Kevin确实选择现有单ES8311的 `audio_io.cpp` 路径，两者都选择有屏UI／SPI显示源，没有选择headless UI。四份记录为 `cached_build_attempts_20261110_duchat_<profile>.json`；日志、配置、ELF、镜像、源码副本及0.85两次真实失败日志在忽略目录 `evidence/duchat-builds-20261110/`。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前47个本地profile对应46个官方变体（另1历史兼容入口），142项尚无源码。累计26个官方变体曾分别通过缓存编译；按当前源码摘要，4项通过、22项历史通过证据过期、5项依赖预检失败、157项未构建（其中15项已有源码）。公共文件或registry变化使未回归的旧记录过期，累计26不等于当前26项通过。

实际对跟踪的 `audio_io.cpp` 执行 `git diff --check`，退出0；对本批未跟踪源码／配置／文档执行 `git diff --no-index --check -- NUL <文件>`，返回1表示新增内容与NUL存在差异，未输出空白错误，保留LF／CRLF提示。没有运行测试套件或独立类型检查，没有设备联网、真实屏幕／手机呈现、麦克风／扬声器／电源／按键验收。牛牛只有现有微雪1.8，屏控修订尚未确认；所有188项实机验收保持not_run。未安装依赖、启动宿主长期服务、刷机／OTA、Git写入、上传或发布；保留并发网关／运行时和自然插话改动。
