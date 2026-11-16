# electronBot AI 基础终端源码适配

后续更新：本页为历史范围记录；Spotpear1.28BOX、MovecallMoji/璀璨、Electron现已用SDK DCS操作保留原GC9A01默认42命令，各自成功完成整板构建。专用GC9107分支未替换。当前依赖963声明、74本地/73官方满足直接条件，最新来源和限制见[本批记录](gc9a01_default_sdk_20261112.md)。

日期：2026-11-11，Asia/Shanghai。新增官方发布变体`electron-bot`的屏幕／按键／语音终端源码候选。六自由度动作、校准及调试服务尚未实现；不把基础语音入口当作机器人整机完成。仅现有微雪1.8实物可验收，electronBot尚未刷机。

## 固定来源

- 小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`下`main/boards/electron-bot/`，以及同提交的`main/audio/codecs/no_audio_codec.cc`。
- [作者文档固定提交](https://github.com/txp666/electronBot-docs/tree/f2f7561cff90c88deb06854de6c20e22000f29ad)：物料、焊接、介绍和下载说明。BOM明确N16R8模组、1.28英寸GC9A01、MAX98357A，以及ICS-43434麦克风／推荐ZTS6672替代；未沿用INMP441型号假设。不同采购替代及PCB实物修订仍需核对。
- 20份实际来源的URL、SHA256和快照在`evidence/electron-source-20261111/source-artifacts.json`，目录按项目已有设置忽略。板目录文件经官方内容API解码，并与API给出的Git blob SHA1核对；没有执行上游代码或下载固件。

原C盘临时元数据已不存在，本轮重新从官方API／发行附件读取固定提交的Kconfig、CMake、完整源码树和release-manifest，保存在本批I盘证据目录。前三项有记录的SHA256与此前矩阵一致，树提交一致且未截断，见`inventory-restoration.json`。适配范围仍为154发布目录／188变体，没有用README过时的138／171替换实际清单。

## 显示与按键

复用现有`waveshare_spi_display.cpp`及GC9A01组件接口：BGR240×240圆屏，SPI3 mode0／40MHz，MOSI10／CLK11／CS12／DC13／reset9；背光46高有效，5kHz／10位PWM，启动先锁存低。RGB565、两份20行DMA缓冲、SPI字节交换，gap0，不交换XY，不发送Spotpear专用面板调参。

固定板类先调用mirrorX，但随后构造`ElectronEmojiDisplay`／`SpiLcdDisplay`又传入配置中的mirrorY。实现使用最终配置mirrorX=false／mirrorY=true，并在panel与LVGL port保持一致；实际安装方向／窗口／颜色仍需实物确认，不能仅由配置称验收通过。

新增独立圆屏布局，卡片按钮高44px，内容与按钮按240圆形范围布置；没有LCD触摸输入。BOOT0初始释放后提供现有说话、卡片确认等操作，初始按住或取消状态不会合成新动作。原厂表情库与机器人动作状态并未接入。

该入口定义`ASSISTANT_BOARD_TFT_DIGITAL`并走现有数字语音路径，没有创建无用的I2C控制器或触摸设备。作者BOM的N16R8采用16MB Flash／Octal PSRAM默认，实物容量未测。复用根16MB分区：双3MB OTA应用槽、4MB模型分区和现有NVS范围。

## 音频及采样率更正

输入为I2S0，BCLK42／WS40／DIN41，LEFT单声道32位槽、16kHz；完整读取32位后转PCM16并沿用固定`NoAudioCodecSimplex`的四位软件提升及饱和裁剪。麦克风具体型号为作者列出的ICS-43434／ZTS6672候选，输入声道、时钟和放大量都未实测，不把软件提升称作模拟增益。

扬声器为I2S1，BCLK18／WS8／DOUT17，LEFT单声道32位槽、16kHz。固定上游设24kHz，但作者BOM确认MAX98357A；上批实际读取的MAX98357A/B规格第19页明确不支持24kHz，本地改用支持的16kHz。网络24kHz复用既有24→16kHz FIR，同一实际发送PCM进入软件AEC参考。

现有`digital_audio.cpp`后端无需新实现：使用数字dummy codec、固定小缓冲转换及一个1秒总期限累计部分读写，静音清零实际PCM。没有独立PA使能GPIO，不宣称物理断电静音、无底噪或无爆音；麦克风采集时钟、功放声道和声学仍待硬件。

## 机器人动作与电量边界

本批只配置显示背光，不对六路舵机GPIO4／5／6／7／15／16设置方向或PWM，不初始化movements／oscillator／控制器，不调用home或运动工具，也不创建原厂端口8080的局域网WebSocket控制服务。没有读写舵机校准／trim存储。代码未发动作不等于机械已经完成安全验收；六自由度适配仍是后续未完成项。

上游电量管理默认100%，用原始ADC2150..2450映射百分比；本地不移植这段未验收估算。GPIO3电池ADC、GPIO14充电检测都不初始化，电量保持未知。睡眠、关机和充电策略未实现。没有复用其他板卡的电源脚或向未确认的机械／供电接口写值。

## 实际依赖预检和公共回归

新目标实际预检：

```text
python -B tools/prepare_cached_firmware_build.py --profile electron-bot --destination I:\xy_board_builds_20261109\electron_preflight_20261111 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1，缺`espressif/esp_lcd_gc9a01=2.0.4`，未创建stage／未编译／未生成electronBot镜像，记录在`native_dependency_preflight_20261111_electron.json`。没有安装依赖、替换为ST7789或下载SDK。

旧敏思K08和T-CameraPlus V1.2在同步所有main文件、注册表、defaults并刷新变动时间戳后，实际执行：

```text
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=minsi-k08-dual reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lilygo-t-cameraplus-s3-v1.2 reconfigure build
```


| 旧入口 | 最终缓存build | app字节 | 应用槽余量 | 模型字节 |
| --- | --- | --- | --- | --- |
| lilygo-t-cameraplus-s3-v1.2 | exit0 | 2,230,032 | 915,696 | 2,203,819 |
| minsi-k08-dual | exit0 | 2,207,184 | 938,544 | 2,203,819 |

两旧入口初次和最终增量构建均退出0；两份镜像checksum／validation hash、ELF符号／段大小、二进制分区解码均退出0。每份188个main文件集合和195份输入摘要与最终工作区一致，独立核对编译宏／选中源／应用身份／QUAD或OCT PSRAM／16MB容量／双OTA槽及模型分区；两份ELF均未包含GC9A01构造符号。SDK保持既有5.5.6-dirty和HEAD880a203ae603d08a94d06aa01e0d6761958e5e9e，保留原NimBLE／idf_as_lib改动。旧目标没有选中ELECTRON宏或GC9A01创建／圆屏布局／电源专用分支；把源文件复制到stage并校验摘要，不等于新目标编译通过。

镜像使用`python -B -m esptool --chip esp32s3 image_info --version 2 <app.bin>`检查，ELF使用`xtensa-esp32s3-elf-nm.exe -S --size-sort -C <app.elf>`和`xtensa-esp32s3-elf-size.exe -A -d <app.elf>`，分区由SDK的`gen_esp32part.py <partition-table.bin>`解码。命令参数、退出码、源文件和产物SHA256均记在`cached_build_attempts_20261111_electron_<旧profile>.json`，不创建新板伪通过记录。

未运行测试套件或独立类型检查；没有修改SDK、Git写操作、上传、发布、刷机或OTA。后续仍需GC9A01依赖、新目标真实构建、机器人运动功能，以及屏幕／音频／按键／网络绑定／提醒／OTA实机验收。

后续电子纸调查：已读取微雪1.54的8份固定来源，两发布修订分别为4MB Flash／2MB QUAD PSRAM和8MB Flash／8MB PSRAM。两套LUT均159字节，波形尾部还单独写电压寄存器，不能直接复用Maza波形。200×200单色、24／26基准帧、BUSY超时及小容量分区仍需独立实现；两变体维持inventory_only，没有新增其profile、构建或实机通过声明。
