# 微雪LCD1.46数字音频与LCD0.85小屏适配

后续更新：LCD1.46已完成首次缓存整板构建，复用重命名后的SPD2010屏幕并增加有界触摸握手／报告读取；SenseCAP和VoCat回归通过。HDP续包组装、实机和声学仍未完成。见[本批说明](spd146_native_20261113.md)。

最新TFT进展见[本轮记录](tft154_adaptation_20261109.md)：25个本地profile对应24个官方变体，164个待源码，10个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

最新阶段见[1.85系列源码记录](waveshare_st77916_adaptation_20261109.md)：21个本地profile对应20个官方变体，168个待源码；新增三板缺驱动未编译。AMOLED1.75C已依据独立厂家页更正32MB容量。以下保留本页对应阶段的数量和证据。

2026-11-09，Asia/Shanghai。继续全部154个发布目录／188个官方变体目标，本轮新增LCD1.46、LCD0.85实际源码入口。现有18个本地profile对应17个官方变体，另含1个历史兼容入口；171个官方变体仍无源码。所有实机验收继续独立记录，牛牛目前只有现有微雪1.8，未确认它的具体屏控修订。

## 两款板不能共用错误的音频前提

| profile／应用名 | 确切硬件与实现 | 当前构建边界 |
| --- | --- | --- |
| `waveshare-lcd-1.46`／`assistant_lcd146` | 412×412圆屏SPD2010显示＋触摸、TCA9554复位、PCM5101输出、独立数字I2S麦克风；16MB Flash／8MB PSRAM | 本机缺SPD2010组件，预检停止，未编译或实机 |
| `waveshare-lcd-0.85`／`assistant_lcd085` | 128×128无触摸ST7789，BGR、双轴镜像、gap2/1；ES7210输入＋ES8311输出，8MB Flash／8MB PSRAM | 首次完整缓存构建退出0；最终改动增量结果另记 |

0.85不是数字I2S麦克风板。固定config保留了一些未使用的触摸常量，但实际板类没有初始化触摸；厂家说明和发布清单均为8MB容量，不能拿1.54的240×240、触摸、16MB配置直接复用。

1.46的固定`NoAudioCodecSimplex`调用明确使用右槽麦克风、左槽播放。输入是16kHz有符号32位I2S承载，转PCM16前右移12位并饱和；扬声器是24kHz、32位左槽，PCM16乘65536后发送。没有独立PA使能GPIO，不能把“dummy codec的mute返回成功”误认为PCM已经静音。

来源均固定在小智提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，相关config、板类、音频类、发布配置和厂家资料摘要写入`board_research_v2.5.2.json`。

- [1.46板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.46)
- [0.85板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-lcd-0.85)
- [数字音频参考类](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/audio/codecs/no_audio_codec.cc)
- [1.46厂家产品](https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-1.46.htm)
- [0.85厂家产品](https://www.waveshare.net/shop/ESP32-S3-LCD-0.85.htm)
- [SPD2010显示2.0.0~1](https://components.espressif.com/components/espressif/esp_lcd_spd2010/versions/2.0.0~1)
- [SPD2010触摸2.1.0](https://components.espressif.com/components/espressif/esp_lcd_touch_spd2010/versions/2.1.0)

SPD2010官方组件的头文件和实现带Apache-2.0 SPDX，API及manifest已只读核对；显示来源提交`12f6ca1182ec48889b17ec570fadaaf267cb336e`，触摸来源`88152bff24d5fbc05ff2ffed3d5208c247711ded`。工程只声明依赖并写本地连接代码，没有复制整个厂家应用或安装组件。组件目录下猜测的LICENSE路径404不推翻实现文件自己的明确SPDX，也不构成更改工程整体许可的依据。

## 显示、输入与供电

1.46本地`waveshare_qspi_display.cpp`使用SPI2：CLK40、CS21、数据46/45/42/41，mode3／40MHz，32位命令、8位参数；采用SPD2010组件自己的默认初始化，不另复制一份厂商表。LVGL用20行DMA双缓冲和SPI完成通知，横向刷新区域按4像素对齐。I2C0 SDA11／SCL10共用扩展器和触摸，TCA9554 P0/P1执行高／低／高复位，触摸地址0x53、IRQ4，GPIO5作PWM背光；触摸失败／越界取消按压，避免误确认。GPIO0按住说话，GPIO7保持电源，PWR6在新的5秒按下后释放保持；不改充电配置。

0.85复用已实现的SPI板音频／I2C／电池保持接线，但显示分支单独设128×128、BGR、镜像和gap。GPIO48留给未实现的RGB环，不再被错误初始化为触摸中断。GPIO4（PLUS）在根页按住说话，在卡片页短按选项／长按松手确认；GPIO5（PWR）仍负责独立2秒电池保持释放，GPIO0（MINUS）不当BOOT。没有把同一电源键同时绑定录音和关机。

`display_layout.h`提供412圆屏和128小屏布局。128屏保留16px字，可横向自动滚动状态／卡片详情、固定操作按钮，配对页保留六位码。无触摸板不能依赖手势滚动；PLUS操作提示也不再称BOOT。0.85采用已落地的8MB双OTA布局（每个应用0x2A0000，模型0x240000），构建、备份、烧录范围和OTA长度继续受实际容量限制。其RGB环、MINUS音量操作、电池读数及传感器未实现，不制造能力记录。

## 数字音频复用

将此前本地`t_circle_audio.cpp`实现收拢为`digital_audio.cpp`／`.h`，T-Circle保留其原始引脚和24kHz I2S／32kHz PDM分支，1.46通过明确常量选择16kHz右槽与24kHz左槽。旧文件移除，CMake按实际板型选择编译文件，未保留两个数字音频实现。

数字后端仍使用既有codec软件音量、dummy PA生命周期、独立I2S输入／输出与有界短读检查。在输出codec接口上转发实际PA的状态，并跟踪静音；没有PA的PCM5101路径真正清零将发送的PCM。1.46输出分块转成32位，不按整段音频动态分配内存。数字麦克风不伪造模拟增益API。

公共采集前端新增真实16→16kHz直通分支，不对已16kHz数据重复重采样，也不扣除不存在的FIR延迟；24→16和32→16的原FIR参数保留，播放参考始终24→16。1.46驱动分支和16kHz实例尚未因其缺依赖而获得编译／声学证据，T-Circle回归只覆盖它们自己的24／32kHz实例。

## 实际命令和验收界限

实际执行1.46缓存准备：

```text
<已有IDF Python> -B tools/prepare_cached_firmware_build.py --profile waveshare-lcd-1.46 --destination <新临时目录> --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1：`Required component is not installed: espressif/esp_lcd_spd2010`，目标目录没有创建、编译没有启动。触摸组件也未在既有缓存中；没有下载或安装，依赖由牛牛准备。该结果不等于板子编译失败或通过。

实际完整构建0.85：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board waveshare-lcd-0.85
```

退出0，首次应用分区检查显示余8%，镜像设置为8MB，模型位于0x5C0000，未刷机。补齐最终镜像方向和无触摸滚动后，同一独立目录另做增量构建；旧五个已编译入口也独立运行`reconfigure build`，用于公共代码回归。最终结果在下方记录。

全部使用已安装IDF5.5.6-dirty及17个既有组件override，Python外网守卫保留；不是干净发行锁，缓存含未使用的旧BSP。没有运行测试套件、独立类型检查、Git写入、上传、发布、启动运行服务、刷机或OTA。

仍需每板实机核对：屏幕颜色／方向／边缘／触摸，PWR和PLUS，数字麦槽位／PCM缩放，软件静音、回声和唤醒，8MB分区恢复、配对／提醒与同板OTA。只有现有微雪1.8实物，不能替其他板验收。

## 暂不登记的相似型号

1.85 Wiki明确提示MSM261已换为ICS-43434；固定源码又以0x04屏幕ID选两套初始化，失败读取可能沿用未初始化缓冲，并重复创建SPI IO。后续移植必须检查ID错误、释放旧IO并明确未知修订，不能照搬该错误流程或直接选其中一套。

1.85C的config.h按CONFIG_VERSION_1_0／2_0选择不同数字／codec音频，config.json的sdkconfig_append为空；进一步读取同提交全局Kconfig，DISPLAY_ESP32S3_TOUCH_LCD_1_85C明确default VERSION_2_0，所以后续基线应走ES7210／ES8311。此前把空append理解为未选版本是不完整判断，已在研究记录更正；这仍不证明购买实物修订。1.85和1.85C当前保持inventory_only，原因是完整屏幕／触摸／音频源码尚未实现，不再把已有Kconfig默认当作阻塞。

## 最终实际结果

最后同步全部固件源码后，六个实际profile分别执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> build`，全部退出0；旧五板此前也各执行`reconfigure build`退出0。0.85首次完整构建退出0，最后增量包含实际镜像方向和无触摸自动滚动改动，未套用首次源码快照冒充最终结果。

六次镜像检查实际执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<对应应用名>.bin`，均退出0；应用名、Flash容量、checksum和validation hash正确。

| profile | 应用字节 | 实际最小应用分区 | 剩余字节 |
| --- | --- | --- | --- |
| waveshare-lcd-0.85 | 2,538,432 | 0x2A0000 | 214,080（约8%） |
| lilygo-t-circle-s3 | 2,507,424 | 0x300000 | 638,304 |
| lilygo-t-circle-s3-v11 | 2,510,416 | 0x300000 | 635,312 |
| waveshare-amoled-1.8-v2 | 2,614,816 | 0x300000 | 530,912 |
| waveshare-lcd-1.54 | 2,546,144 | 0x300000 | 599,584 |
| waveshare-lcd-1.83 | 2,615,552 | 0x300000 | 530,176 |

另外运行`python -B <IDF>/components/partition_table/gen_esp32part.py --flash-size 8MB build/partition_table/partition-table.bin`读取0.85实际二进制分区表，退出0并输出合法8MB分区：app0@0x20000、app1@0x2C0000、model@0x5C0000；两应用各2688KB、model2304KB。实际模型2,203,819字节，小于0x240000模型分区。当前应用约8%余量是有限容量约束，后续功能增长仍须重新检查。

每份最新记录比对46份源码／配置，均匹配当前工作区；0.85比对的是专用板型partitions.csv，未错误地拿16MB根分区作输入。`cached_build_attempts_20261109_digital_<profile>.json`记录实际命令、SDK、源码和产物摘要；`native_dependency_preflight_20261109_digital.json`独立记录1.46缺依赖、未启动编译。日志、镜像、ELF、模型、分区、输入快照和执行脚本保存在Git忽略的`evidence/waveshare-digital-20261109/`，没有复制到网关自动OTA路径。

SDK只读复查HEAD仍为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，已有NimBLE及idf_as_lib修改保留，镜像自报5.5.6-dirty。已有LVGL flag API弃用告警保留；新数字音频／供电编译没有新增相关告警。这六份本地成功不证明1.46独有16kHz／SPD2010分支已编译、软件静音声学效果正确或所有依赖可干净重建。

当前17个官方变体有源码，其中6个通过当前源码的缓存构建、11个有源码仍未编译、171个无源码；本轮188项实机验收全部未执行。全量目标继续，不标为完成或整体阻塞。全部宿主构建进程已退出，没有安装、测试套件、类型检查、运行服务启动、Git写入、上传、发布、刷机或OTA。

最终编辑审查：`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp`退出0；30份本轮相关未跟踪文件各执行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。矩阵再次确认6个passed_cached_sources均有当前输入摘要匹配的记录。运行时、网关、control_protocol及自然插话并发改动未被编辑、覆盖或回滚。
