# 星智ABS2.0与0.85方块源码接入

后续更新：本页保留此前候选范围；Magiclick2.4与星智0.85WiFi现已移除各自外部NV3023依赖，保留原默认25／专用54命令和reset时序，并分别完成首次整板构建。最新来源与实机边界见[本批记录](nv3023_sdk_20261112.md)。

后续金属版离散键接入见[本轮记录](xingzhi_metal_adaptation_20261109.md)：28个本地profile对应27个官方变体，161个待源码；以下保留本页对应阶段的数量和证据。

2026-11-09，Asia/Shanghai。继续小智v2.5.2全部154个发布目录／188个变体的目标。本轮新增两个实际基础终端源码入口，当前27个本地profile对应26个官方变体，另含1个历史兼容入口；162个官方变体仍待源码。源码候选、独立编译和实机验收分别记录，未完成的设备专属功能不计为完成。

## 硬件事实

| profile／应用名 | 显示与输入 | 音频与电源 |
| --- | --- | --- |
| `nologo-xingzhi-abs-2.0`／`assistant_xzabs20` | 240×240 ST7789，RGB、反色、无镜像／gap；SPI3 mode3／80MHz，MOSI10／CLK9／DC8／CS14／RST18／BL13；BOOT0，无坐标触摸 | 单ES8311，24kHz输入输出，I2C0 SDA41／SCL42，MCLK5／BCLK6／WS4／输入17／输出15／PA21；供电控制47初始拉高，PWR检测48 |
| `nologo-xingzhi-cube-0.85tft-wifi`／`assistant_xz085` | 128×128 NV3023，BGR、仅Y镜像、不反色、gap0；SPI3 mode0／40MHz，MOSI10／CLK9／DC8／CS14／RST18／BL13；BOOT0，无触摸 | 数字I2S16kHz左槽采集、24kHz左槽输出，MIC WS4／BCLK5／DATA6，speaker BCLK15／WS16／DATA7；RTC21高、辅助GPIO45低 |

两份候选按固定上游发布默认16MB Flash／Octal PSRAM构建。没有测量任意购买实物的容量；特别是星智0.85不能套用微雪0.85的8MB／ST7789／gap2/1／双轴镜像。屏幕尺寸相同不等于控制器、方向或容量相同。

依据固定提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的config.h、config.json、板类及电源代码。ABS头里的`AUDIO_CODEC_USE_PCA9557`符号并未被固定实际Es8311AudioCodec实现引用；该音频类使用GPIO PA，因此此源码入口按GPIO21路由，仍需实物／电路图确认具体修订。

- [ABS固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-abs-2.0)
- [0.85固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-cube-0.85tft-wifi)
- [固定ES8311实现](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/audio/codecs/es8311_audio_codec.cc)
- [固定组件清单](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/idf_component.yml)
- [NV3023组件1.0.1](https://components.espressif.com/components/78/esp_lcd_nv3023/versions/1.0.1)

NV3023的实际namespace是`78`；`espressif/esp_lcd_nv3023`和`waveshare/esp_lcd_nv3023`路径404后，按上游manifest找到`78/esp_lcd_nv3023 ==1.0.1`，不能把猜测路径的404当作没有该驱动。注册表关联源码`MakerM0/esp_lcd_nv3023@15dae9531b4fc031bcc12dea1ddfe02517ca7711`的API／manifest／实现已只读核对；SPI IO宏为mode0／40MHz，头和实现明确Apache-2.0 SPDX。没有安装它或复制整个组件进工程。

## 接入范围与仍未完成的功能

`tft_board.h`增加两个型号的确切常量；ABS复用已有单ES8311输入输出句柄和SPI显示，0.85复用数字音频并单独选择NV3023工厂、静态初始化和显示方向。GPIO0沿用根页按住说话、卡片短按选择／长按松手确认；不复制上游应用／MCP工具和电池算法。

`nv3023_board_init.h`仅转换0.85板类中的54条有效初始化数据为C++静态存储，注释不入表，命令、有效参数、长度和延迟逐项一致。MIT全文及固定仓库版权通知保留在`main/third_party/xiaozhi_nv3023/`；Docker该型号导出路径补带此许可目录，但本轮未执行Docker导出。驱动仍是单独的Apache组件依赖，未改变工程整体许可。静态数据比对记录保存在`evidence/xingzhi-builds-20261109/nv3023-command-review.json`，不是驱动运行或显示效果证明。

NV3023遵循固定板类：16位像素、BGR、仅Y镜像、不反色、gap0；LVGL登记也设置Y镜像，防止其重新应用旋转时覆盖正确面板方向。复用128屏中文配对码、长文自动滚动和固定卡片按钮，不要求无触摸板做手势滚动。供电代码按该板初始状态释放RTC21旧保持并拉高，GPIO45设低；不把其他板的PA／充电引脚写入该板。

ABS2.0按固定电源类只初始化供电控制47为高。其软件关机依赖USB ADC判断和重复脉冲，不是简单拉低电池保持；本轮没有照搬脉冲或把PWR48当通用录音键。**软件关机、USB／电池遥测仍未实现**，供电启动代码不等于电源管理已验收。

ABS的ML307R蜂窝联网、SD缓存、震动马达、专用音量键和自动深睡尚未接入当前基础Wi-Fi终端；它们仍是后续范围，不因基础语音源码存在就说设备专属能力已完成。没有自动启动蜂窝、马达或其他执行器。0.85的充电／电池和深睡同样未实现，不制造读数。

## 实际构建和依赖预检

0.85实际执行：

```text
<已有IDF Python> -B tools/prepare_cached_firmware_build.py --profile nologo-xingzhi-cube-0.85tft-wifi --destination <新临时目录> --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1，`Required component is not installed: 78/esp_lcd_nv3023`，没有创建目标目录、编译或安装依赖。`native_dependency_preflight_20261109_xingzhi.json`保存命令、退出码和日志摘要，原始日志放Git忽略的`evidence/xingzhi-builds-20261109/`。缺依赖时不把只读源码资料当成已安装组件；依赖由牛牛准备。

ABS实际运行：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board nologo-xingzhi-abs-2.0
```

并对十个已有profile在各自既有隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`核对公共代码。使用同一已安装SDK5.5.6-dirty、17个本地source override和Python外网守卫，本轮回归子进程设置`IDF_PY_BUILD_JOBS=2`限制单次编译并行度。实际退出和最终镜像记录在下方，不把计划命令当成功。

仍需硬件验收。牛牛目前只有现有微雪1.8，ABS／NV3023方块都不在手；需取得对应硬件后分别核对Flash/PSRAM、屏幕方向／颜色／边缘、BOOT卡片操作、供电和关机、PA21、采样／静音／唤醒／回声、配对／提醒及同板OTA。无实机证据不得将该阶段写成完整交付。

## 金属版调查结果

金属1.54的PWR控制48／检测47与ABS交换。CST816x在I2C0地址0x15返回`x=20/40/60,y=600`等虚拟触摸按键，README明确用三个触摸按键替代物理操作键。这些不是240×240屏幕坐标，不能丢掉越界值后声称触摸已支持，也不能把config中的BOOT0当作可用的外部操作键。金属版还存在新旧电源波形差异，本轮仅研究、保留inventory_only，后续需实现真实键码录音／卡片输入及故障取消。

没有运行测试套件、独立类型检查、安装依赖、启动长期服务、Git写入、上传、发布、刷机或OTA。

ABS首次编译实际失败：GCC在既有ESP-DSP的`modules/conv/float/dspi_conv_f32_ansi.c:184`报`internal compiler error: Segmentation fault`，IDF退出2、包装脚本退出1；并非普通C/C++诊断。失败日志保留为cached-build.log。已核对该输入源码与工作区现有缓存SHA256一致，未编辑SDK或组件；在同目录将IDF_PY_BUILD_JOBS=2后执行reconfigure build重试，结果另记。原进程确认终止后才重试，没有因观察超时重开。

## 最终实际结果

ABS在同一SDK／原隔离目录降为2个编译任务后执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=nologo-xingzhi-abs-2.0 reconfigure build`，退出0，未改ESP-DSP源码或SDK。失败阶段的cached-build.log、成功重试的xingzhi-retry.log分别保留，没有把首次失败改成成功。

十个已有入口的`reconfigure build`也均退出0，作为公共代码回归。十一个实际镜像分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用身份、8／16MB镜像设置正确，checksum和validation hash有效。没有串口连接。

| profile | 应用字节 | 实际应用分区剩余 |
| --- | --- | --- |
| nologo-xingzhi-abs-2.0 | 2,531,232 | 614,496（3MB分区约20%） |
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

每份最新记录核对53份固件源码／配置／许可摘要，并额外检查main源文件集合无缺项；记录时全部与工作区一致。旧十份构建没有编译NV3023分支，新的NV许可目录是在构建后补齐到这些源码快照，实际编译输入与应用镜像不受此文档同步影响；这不提供NV3023编译证据。ABS使用完整初始main副本，NV3023独立profile仍build=not_run。

`cached_build_attempts_20261109_xingzhi_<profile>.json`记录命令、首次编译器错误／成功重试、镜像和53份摘要；成功／失败日志、应用、ELF、模型、分区、配置、输入快照、第三方许可和本轮命令脚本保存于Git忽略的`evidence/xingzhi-builds-20261109/`，未复制到网关自动OTA路径。SDK重新只读核对HEAD仍为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，原NimBLE／idf_as_lib改动保留，镜像自报5.5.6-dirty。已有LVGL flag API弃用告警保留，不称干净发行依赖构建。

当前26个官方变体有源码，其中11个有当前源码的缓存构建证据、15个已有源码尚未编译；162个官方变体无源码。188项本轮实机验收全部未执行，ABS蜂窝／SD／马达／关机等专属功能仍未实现。所有本轮构建进程已退出，目标继续进行；没有安装依赖、测试套件、独立类型检查、运行服务启动、Git写入、上传、发布、刷机或OTA。

最终编辑审查执行`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp tools/build_firmware.ps1`退出0；32份相关未跟踪文件各执行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。矩阵确认11份passed_cached_sources均有当前输入摘要匹配。运行时、网关、control_protocol及自然插话并发改动未编辑、覆盖或回滚。
