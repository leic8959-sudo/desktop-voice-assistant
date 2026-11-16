# ESP-SparkBot 基础终端适配

日期：2026-11-10，Asia/Shanghai。新增官方变体`espressif-esp-sparkbot`的基础Wi-Fi语音终端入口；没有将摄像头、机器人运动等整板功能记为完成。仅现有微雪1.8可供实机验收，本板未刷机。

## 来源和接线

- 小智v2.5.2固定提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的`main/boards/espressif/esp-sparkbot/config.h`、`config.json`、`esp_sparkbot_board.cc`。
- [厂家项目](https://gitee.com/esp-friends/esp_sparkbot)，本轮读取master提交得到`d42c5b43e25168fa3592a9c3d113ab5631f47023`，再用该提交读取factory_demo_v1的BSP头／源码／IDF5音频实现，以及volc_rtc_sparkbot的SparkBot `board_def.h`。
- [厂家开源硬件入口](https://oshwhub.com/esp-college/esp-sparkbot)，本轮未下载原理图，不能从入口链接推断电路已独立验收。
- 10份实际来源的URL／SHA256和本地快照在`evidence/sparkbot-source-20261110/source-artifacts.json`；该证据目录按项目已有设置忽略。

| 部件 | 本地配置 |
| --- | --- |
| 屏幕 | ST7789、RGB、240×240，SPI3 mode0／40MHz，MOSI47／CLK21／CS44／DC43／RST NC，invert=true，无镜像／XY交换，gap0 |
| 显示缓冲 | 两份240×20行内部DMA缓冲，RGB565，SPI字节交换，复用现有LVGL port和240布局 |
| 音频 | 单ES8311，I2C0 SDA4／SCL5，MCLK45／BCLK39／WS41／DIN40／DOUT42；MCLK来源为ESP，16kHz单声道输入输出，默认30dB输入增益 |
| 交互 | BOOT0初始释放后说话及卡片操作，无LCD触摸；不把顶部／侧面电容触摸误当LCD坐标 |
| 内存／分区 | 固定发行默认16MB Flash／Octal PSRAM；复用16MB双3MB OTA槽和4MB模型分区，NVS9000＋4000保留；模组实际容量未验收 |

16kHz播放通过既有FIR将网络24kHz降采样，同一实际播放PCM进入软件AEC参考。没有复制上游相机或底盘工具。

## GPIO46 共用背光和功放

固定板类明确注释display/PA IO冲突，因此其输出关闭回调不执行关闭。厂家factory BSP也将背光定义为46，而音频示例将PA定义为46。不能为同一GPIO同时注册背光PWM和codec功放开关。

本地在板级启动时先锁存GPIO46低并配置输出；ST7789初始化完成后将它拉高，运行期保持稳定。ES8311的PA pin为NC，内部DAC静音回调和麦克风增益回调直接检查实际驱动结果。会话关闭时保留I2S数据时钟并静音DAC，复用TX auto-clear零数据，避免codec关闭拉低共用引脚。这里没有独立的硬件PA切断；使能时爆音、静音残音和功放底噪都未实测。

显示只支持DCS28／29的面板消隐和恢复，不切断GPIO46，因此背光仍可能发光。初始化显示状态为100%；偏好只接受0或100，中间亮度及夜间模式在修改恢复缓存前返回`ESP_ERR_NOT_SUPPORTED`，现有协议会返回`display=false`。这样一次被拒绝的75%偏好不会破坏空闲消隐后的恢复值。要应用这一板型的显示偏好，应选择100或0并关闭夜间模式；这不是连续调光或真正的电气熄背光能力。

共享GPIO只能在显示初始化失败、整个启动终止时拉低；运行期静音及显示消隐均不控制它。尚未用实物确认共用网络、电平和显示开关效果。

## 源码范围

复用现有`waveshare_spi_display.cpp`和单ES8311音频实现，在`tft_board.h`／`board_profile.h`加入独立引脚和身份，接入板级供电、启动错误处理及显示偏好分支；没有增加重复屏幕／codec驱动。旧板在新宏未选中时保留原逻辑。

未实现：OV2640视频／相机工具、顶部及侧面电容触摸、BMI270、C2底盘UART38/48／电机／RGB灯、原厂MCP工具、硬件电池／充电策略、睡眠。特别是没有发送上游启动时的`w2`或任何移动命令。基础终端入口不是整机功能完成状态。

## 构建和验证

复用既有ESP-IDF5.5.6、工具、Python及17份本地组件，在隔离I盘目录使用offline_guard构建，没有安装依赖或修改SDK。未执行测试套件或独立类型检查。

实际完整目标命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board espressif-esp-sparkbot
```

最终增量在同步全部main文件／注册表／defaults并刷新变更文件时间戳后，分别执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，目标为SparkBot及旧Cardputer Adv。后者用于检查共用单ES8311／实际静音分支与无PSRAM手动模式，不是SparkBot硬件证明。


| 入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| espressif-esp-sparkbot | exit0 | 2,237,888 | 907,840 | 2,203,819 |
| m5stack-cardputer-adv | exit0 | 1,736,944 | 1,015,568 | 2,203,819 |

SparkBot首次完整构建和最终增量、旧Cardputer Adv增量均退出0。两份镜像checksum及validation hash、ELF符号／段大小、分区解码命令均退出0；每份177个main文件集合及184份输入摘要与最终工作区一致。选中编译源、宏、应用身份、PSRAM／内部RAM配置、16／8MB容量和双OTA分区单独核对；Cardputer不链接AEC／AFE且不加载模型，SparkBot链接软件AEC。SDK仍为既有5.5.6-dirty，保留其NimBLE及idf_as_lib改动；缓存不代表干净发行依赖验收。日志与产物已归档。镜像使用`python -B -m esptool --chip esp32s3 image_info --version 2 <app.bin>`核对；ELF执行`xtensa-esp32s3-elf-nm.exe -S --size-sort -C <app.elf>`和`xtensa-esp32s3-elf-size.exe -A -d <app.elf>`；分区使用SDK的`gen_esp32part.py <partition-table.bin>`解码。每条实际参数／日志／退出码、输入和产物摘要保存在`cached_build_attempts_20261110_sparkbot_<profile>.json`。

所有引脚修订、模组容量、显示方向／颜色／消隐、PA共用电平与音频MCLK／增益／声学、网络配对、提醒、OTA均未实机验收。未执行Git写操作、上传、发布、刷机或OTA。

后续VoCat前置调查：已缓存固定小智及厂家BSP共12份来源。V1.0的DIN15／PA4／LCDRST3低有效，与V1.2的DIN3／PA15／LCDRST47高有效冲突；GPIO48为后者codec供电。固定应用40MHz／24kHz与新BSP80MHz／48kHz默认配置不同。两来源的184条ST77916命令完全相同，但不同于现有本地四套命令表；ST77916组件本机仍无缓存。该板维持inventory_only，尚未增加profile／源码实现或编译结果；详情已加入板型研究JSON。
