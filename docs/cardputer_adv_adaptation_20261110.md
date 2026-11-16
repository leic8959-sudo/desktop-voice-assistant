# Cardputer Adv基础语音与实际无PSRAM目标构建

日期：2026-11-10，Asia/Shanghai。新增 `m5stack-cardputer-adv` → `assistant_cardadv`，8MB Flash、无PSRAM、240×135 ST7789、ES8311与TCA8418终端按键。实际缓存目标构建、镜像、二进制分区和RAM段检查通过；没有Cardputer实物，不能视为已运行验收或完整电脑功能移植。

## 固定来源与范围

读取小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的config、config.json、板类、键盘源／头、键盘配网UI源／头和README，并核对[厂家Cardputer Adv页面](https://docs.m5stack.com/en/core/Cardputer-Adv)。9项来源的URL／原文／SHA-256在research与忽略目录 `evidence/cardputer-adv-source-20261110/`，没有执行上游脚本、安装依赖或复制完整应用类。

固定README的DIN／DOUT表与实际config／类相反，厂家表确认G46为ASDOUT、G42为DSDIN。本地采用MCU DIN46／DOUT42，不把那张反向表当接线依据。资料中的型号为Stamp-S3A／ESP32-S3FN8、8MB无PSRAM，不套用普通Cardputer数字音频或AtomS3R的引脚。

新增 `cardputer_adv_display.cpp`、`cardputer_adv_keys.h/.cpp`及板级manifest／defaults／8MB partitions。公共改动限CMake、板配置／能力、启动失败处理、单codec选择与实际mute回调、文本布局按真实宽度扩展。复用既有内部内存手动语音和ST7789／LVGL／ES8311／I2C封装，未增加第三方驱动副本，不改规则文件或项目整体LICENSE。

本批是基础语音终端：BOOT／Enter录音及卡片操作、音量、显示、联网／手机配网／绑定码、播放和提醒走现有路径。完整文字输入、键盘Wi-Fi编辑器、亮度快捷键、BMI270、ADC电量、SD和IR没有接入；未把阅读其源文件等同成功实现。

## 240×135显示和文本卡片

ST7789V2使用SPI3 mode0／40MHz、sio_mode，MOSI35／CLK36／CS37／DC34／RST33，RGB565、invert、swapXY、mirrorX。保留固定配置的可见窗口gap(40,53)，不在LVGL再叠加一遍offset。GPIO38高有效背光，8位／256Hz PWM，启动先锁存低，UI就绪后才设置亮度；没有照搬上游最少30%亮度限制，因此现有屏幕关闭功能仍可熄屏。

单个240×10行内部DMA缓冲4,800字节，LVGL port负责一次字节交换和传输完成。显示方向、窗口边缘、色序、256Hz亮度及熄屏后台运行仍需实机，构建不证明图像已正确显示。

内部文本模式改为使用实际board宽度：卡片标题／状态／正文和按钮在240宽画面内布局，不局限于AtomS3的128宽。135高时正文y34、高75（到108），操作行y113、高20（到132）；两个按钮各118宽、间隔4，总宽240。已有128高AtomS3坐标仍为正文34–101、操作106–125，真实OLED布局保持原尺寸。

绑定码、错误、提醒、DND未读和卡片确认保留，不创建点云和本地唤醒提示。实际中文字形、滚动、按键选择和卡片替换／取消尚未设备验收。

## TCA8418有限轮询与输入取消

键盘I2C0地址0x34，与codec共用SDA8／SCL9，400kHz。7行8列扫描，GPIO11为外部上拉的IRQ输入；使用现有20ms按键任务轮询，没有新增键盘任务或ISR。配置只拥有七行／八列和相应配置位，逐次通过寄存器读回核对，不改BMI270或其他共享设备。

按固定驱动的事件协议读取：状态0x02、事件数0x03、FIFO0x04，每轮最多十条，按code＝row×10＋col＋1解码。原7×8重映射为逻辑row＝column%4、logical_column＝raw_row×2＋右半列标志，维护所有56键的bitmask并去除重复状态。实际接入：Enter逻辑(2,13)、分号(2,11)音量上、句点(3,11)音量下；对应原始事件码67／57／58。BOOT0同时保留，Enter与BOOT都走同一手动语音／卡片owner。

只接受一个明确的按压来源。初始等待释放，组合键、未支持字符／修饰键或按压中来源切换均返回Invalid，现有主按键任务会取消录音／卡片按压，而非把它当正常松手确认。短于轮询间隔的完整按压／释放可能被合并成没有动作，键盘实际扫描／去抖与开机按住的状态重建必须验机。

读取前后检查FIFO overflow。I2C错误、无效事件或溢出锁存键盘故障，不重试破坏性的FIFO读取，也不猜丢失的按键状态；本次返回Invalid取消当前动作，之后禁用键盘直到重启。BOOT0在观察到松开后仍可重新录音／操作卡片。只清key interrupt位，不抹掉两次状态读取之间可能出现的overflow；轮询不依赖新IRQ边沿。没有宣称键盘故障已恢复或设备已接受动作。

单次I2C调用超时100ms，一次轮询包含多次调用，不能将100ms说成整个FIFO处理上限。高频输入、FIFO溢出、按住启动、错误后的BOOT回退、音量长按和Enter长按卡片确认均未实机验证。

## ES8311与内部内存模式

单ES8311、mono24kHz，I2C0 SDA8／SCL9，I2S0 BCLK41／WS43／DIN46／DOUT42，MCLK NC并选择BCLK派生codec时钟。沿用30dB MIC增益和公共FIR转换至16kHz上行。NS4150B没有本地PA控制引脚，不调用外置PA helper，也不声称能物理关闭功放。

输出mute先保留codec_dev状态，再显式检查它可能忽略的实际ES8311 DAC mute返回；静音PCM仍归零发送。没有用一个不存在的GPIO补成功。功放上电声、DAC静音／重开、MIC增益、24kHz时钟和录音声学表现需实物。

官方profile显式启用内部内存手动模式：不初始化WakeNet／AFE／AEC，不上传待机PCM，保留manual request／ready／PCM／end及媒体暂停归属；读取错误、音频不连续或队列溢出取消本轮。没有本地唤醒、自然插话或媒体语音唤醒。发送32音频槽＋1控制槽、播放8槽及四协议栈请求合计71,028字节，但这只是局部预算，不含Wi-Fi／TLS／DMA／LVGL／producer／提示音／OTA峰值。

实际链接DRAM data20,484／BSS129,400字节（含64KiB LVGL池），外部RAM数据／BSS为0；符号检查无AFE／AEC／模型初始化或大播放参考环。链接脚本仍有用于跳过flash rodata地址空间的NOLOAD ext_ram.dummy，不将其当PSRAM数据。

没有实测启动／配网／TLS重连／手动录放音／提示音／OTA的internal_free、largest_block和任务栈水位。慢ready或网络背压会使有限发送队列取消录音；不能由无PSRAM链接通过推断稳定运行。电池ADC10及充电状态未实现，电量继续unknown，不按1750mAh规格推算剩余电量。

## 真实构建、镜像与分区

实际执行：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board m5stack-cardputer-adv
```

准备／完整目标构建退出0，隔离stage为 `I:\xy_board_builds_20261109\xy_cached_26d3940eb638`。复制17个既有组件，Python offline_guard阻止外部socket／DNS，没有下载或安装。目标实际编译cardputer_adv_display／keys、main／audio／power／display UI，不是只编旧入口。

旧1.43C同步本批源／刷新变化文件时间后执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-touch-amoled-1.43c reconfigure build`，退出0并实际重编公共文件。两组进一步执行：

```powershell
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -S --size-sort -C build/<app>.elf
xtensa-esp32s3-elf-size.exe -A -d build/<app>.elf
python -B <SDK>/components/partition_table/gen_esp32part.py build/partition_table/partition-table.bin
```

均退出0。Cardputer独立项目名assistant_cardadv、8MB、checksum／validation hash匹配；所选源与宏为真实官方Cardputer profile，不是临时参考覆盖，链接TCA／ST7789／ES8311，不链接外置底座或AFE／AEC。1.43C仍选默认PSRAM／CO5300／双codec和唤醒入口。

二进制分区表确认两板均保留NVS0x9000／0x4000、app0起0x20000、app1起0x2c0000，两槽各0x2a0000，model起0x5c0000、大小0x240000，最大终点0x800000。Cardputer仍生成模型分区文件但运行期不加载，不能因此声称可本地唤醒。

| 目标 | 应用字节 | 单OTA槽剩余 | 模型字节 | 模型分区剩余 |
| --- | ---: | ---: | ---: | ---: |
| Cardputer Adv／无PSRAM | 1,736,944 | 1,015,568 | 2,203,819 | 155,477 |
| 旧1.43C／PSRAM | 2,544,960 | 207,552 | 2,203,819 | 155,477 |

两组各166个main文件集合、173项输入摘要、真实编译宏／生成配置、合并defaults、分区来源和工作区匹配；各21项产物归档、源快照与日志在 `evidence/cardputer-adv-source-20261110/builds/`，记录为 `cached_build_attempts_20261110_cardadv_<profile>.json`。使用SDK5.5.6-dirty／17个local override，不是干净发行依赖构建。镜像可容纳也不代表上传、OTA、物理刷入或NVS读回通过。

当前98官方源码／99本地入口含1历史／90待源码，累计54官方变体曾缓存编译通过；当前2 passed（Cardputer／1.43C）、52 stale、6历史failed、128 not_run（其中38有源码），全部188实机not_run。实际实物仍只有微雪1.8，屏控修订未确认，全量目标继续。

未运行测试套件或独立类型检查、安装依赖、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。协议／网关／运行时、无屏绑定UI、Atom公共识别和底座封装保护摘要保留，未修改人工决策逻辑。

收尾实际执行 `git diff --check` 退出0；21项新增／变更文件逐项 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1为新增差异。9项来源、两组各166个main文件／173项输入／21项归档产物摘要匹配；原98个profile与其他research完整保留，6项保护文件摘要相同。核对键盘三种事件映射、config与厂家音频数据流及240×135卡片坐标；矩阵摘要／目标passed_cached_sources／全部188实机not_run匹配。两个构建进程均已结束，没有实物运行或升级验收。
