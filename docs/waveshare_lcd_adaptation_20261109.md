# 微雪LCD1.54／1.83板级适配

最新阶段见[1.46／0.85及数字音频记录](waveshare_digital_adaptation_20261109.md)：18个本地profile对应17个官方变体，171个待源码，6个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

2026-11-09，Asia/Shanghai。继续小智v2.5.2全部154个发布目录／188个变体的适配。本轮新增两份实际源码入口，当前16个本地profile对应15个官方发布变体（另有1个历史兼容入口），173个变体仍无源码；源码、缓存构建与实机分别记录。

## 确切硬件与输入方式

| profile／应用身份 | 显示／触摸 | 音频／供电／存储 |
| --- | --- | --- |
| `waveshare-lcd-1.54`／`assistant_lcd154` | 240×240，ST7789，SPI3 mode3／40MHz，RGB565、反色、gap0；CST816S，SDA42／SCL41，复位47／中断48 | ES7210输入＋ES8311输出，24kHz，MCLK8／BCLK9／WS10／输入11／输出12／PA7；GPIO2电池保持，GPIO5 PWR输入，GPIO0是音量减，GPIO4音量加；根页触摸按住说话，不占GPIO0作BOOT |
| `waveshare-lcd-1.83`／`assistant_lcd183` | 240×284，ST7789，SPI3 mode0／24MHz；CST816S，SDA15／SCL14，复位39／中断13 | ES7210输入＋ES8311输出，24kHz，MCLK16／BCLK9／WS45／输入10／输出8／PA46；AXP2101的ALDO1麦克风电源，GPIO0按住说话；单线SDMMC CLK2／CMD1／D0=3，不自动格式化 |

两款厂家资料均列16MB Flash／8MB PSRAM，仍使用现有16MB分区和Octal PSRAM配置。1.83的固定config宏写SPI mode3，但实际板类创建IO时用mode0／24MHz；本实现依据实际调用，不只读取宏。其余屏幕镜像／坐标变换均为false，真实色彩、边缘位置及触摸方向仍要实物确认。

固定小智提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`下各板的config.h、板类和1.54的power_manager.h被用作硬件事实来源；没有复制上游应用类、网络协议、电池估算或工具注册代码。本地使用既有ESP-IDF ST7789、`esp_lcd_touch_cst816s1.1.2`、`esp_lvgl_port2.9.0`、`esp_codec_dev1.5.11`及现有SR组件，不新增安装。

- [1.54固定板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.54)
- [1.83固定板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.83)
- [1.54厂家产品](https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-1.54.htm)
- [1.83厂家Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.83)

英文产品页／1.54英文Wiki请求返回404，改读固定README给出的中文厂家链接；没有把404当作板型不存在。所读1.83厂家BSP头的音频／LCD／SD引脚与固定板源码一致，BSP本身未安装。

## 源码闭环

- `waveshare_spi_board.h`仅包含两个已核对型号的常量；未知型号编译报错。`waveshare_spi_display.cpp`持有I2C0、SPI3、ST7789／CST816S、20行DMA双缓冲和PWM背光。触摸读取失败或坐标越界时重置按压，防止通信中断被当成点击确认；错误与恢复按状态变化记录。
- LVGL9.6的`lv_indev_reset()`会向原按压对象发送`LV_EVENT_INDEV_RESET`，公共按住说话按钮也处理该事件以停止录音。不能只清空指针而让录音保持，也不伪造CLICKED。
- `waveshare_spi_audio.cpp`初始化独立ES7210／ES8311句柄和共用全双工I2S0，选MIC1送既有单声道FIR／软件参考。保留驱动的格式／启停调度，但另记录其返回值，避免codec1.5.11忽略data接口错误后冒称open成功。音频读写检查实际字节，短读／短写在同一1秒截止时间内累积；失败录音清零并返回错误。
- `board_power.cpp`复用保守ALDO1设压／按位启用／读回处理1.83麦克风。1.54电池保持GPIO2开机拉高，GPIO5只作输入；PWR任务忽略开机期间按住，新的持续2秒按下才释放保持，USB仍供电时不声称机器已断电。充电参数、电池读数和其他电源轨没有被重写。
- `display_layout.h`分别提供240×240／240×284布局。小屏卡片长文字可在独立区域滚动，操作按钮保持固定，路线不叠加大箭头；配对标题换行，字体16px。1.54根页按住说话按钮独立于电源／音量键。1.54的独立音量键尚未映射为助手音量操作，传感器／电池状态未实现，不伪造读数。
- 1.83的SD采用厂家确切单线引脚、最多5个打开文件、禁止格式化。SD错误由已有storage层记录，不阻止语音启动；1.54未建立SD引脚证据，保持无本地SD快照能力。
- 每款有独立应用名、依赖manifest、构建／OTA身份；没有只添加显示尺寸而把另一块板的固件当作适配。

## 实际构建与限制

分别实际执行：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board waveshare-lcd-1.54
# 第二次 -Board waveshare-lcd-1.83
```

两次首次构建均在应用编译阶段失败：CST816S公开C宏的designated initializer顺序与当前SDK的C++字段顺序不兼容。已改为零初始化后逐字段赋值相同I2C参数；未修改或更新组件，失败日志保留。修正后同步最终音频字节检查和触摸取消处理，在各自隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`，修正后的重新配置构建退出0；随后改用触摸组件当前get_data API并再次增量构建，退出0。

同时对已有两款T-Circle及微雪1.8-v2独立目录运行相同的最终增量命令，用于核对公共源码回归，结果分别记录。构建使用既有SDK5.5.6-dirty和17个本地组件override，Python外网连接／解析由守卫拒绝。缓存仍包含无关的旧微雪BSP，不是干净发行依赖锁。未运行测试套件或独立类型检查，没有安装、Git写入、上传、发布、刷机或OTA。

牛牛目前只有现有微雪1.8；两款新LCD都没有实机证据。需分别验收屏控／触摸修订、颜色与坐标、录音通道和增益、播放／回声、触摸按住及故障取消、电源键、SD、配对、提醒和同板OTA。构建成功不能替代这些实机条件。

## 下一批已查差异

固定源码调查已记录1.46／1.85／1.85B／1.85C：1.46为412×412 SPD2010，1.85为360×360 ST77916，原版采用独立数字I2S16kHz输入／24kHz输出；1.85B改为ES7210／ES8311且引脚不同；1.85C有条件编译的不同音频硬件／采样率。四项继续inventory_only，不能按相近商品名共用固件。

4B为RGB ST7701／GT911和扩展器功放，4.3C另涉及800×480 RGB、GPIO0作为显示数据线、CH32V003扩展器及BSP的LVGL9.4依赖约束；本轮只读BSP资料，不直接套用SPI代码、GPIO0按键或LVGL9.6依赖。

## 最终构建证据

五份profile最终分别执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> build`，均退出0；每份在此前已完成相同profile的`reconfigure build`。新增LCD最初完整构建的CST816S宏错误日志保留，修正后重用已编译依赖继续完成链接，未把首次失败写成通过。

随后对五份应用各运行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，应用名正确、checksum和validation hash有效；没有连接串口。

| profile | 应用字节 | 3MB应用分区剩余 | 本轮实机 |
| --- | --- | --- | --- |
| waveshare-lcd-1.54 | 2,546,144 | 599,584（约19%） | 未执行 |
| waveshare-lcd-1.83 | 2,615,552 | 530,176（约17%） | 未执行 |
| lilygo-t-circle-s3 | 2,507,168 | 638,560（约20%） | 未执行 |
| lilygo-t-circle-s3-v11 | 2,510,144 | 635,584（约20%） | 未执行 |
| waveshare-amoled-1.8-v2 | 2,614,816 | 530,912（约17%） | 未执行 |

每份记录比对43份固件源码／配置，记录时全部与工作区一致。`firmware/assistant_espidf/cached_build_attempts_20261109_lcd_<profile>.json`含失败／成功命令、SDK、override、镜像及输入摘要；原始日志、应用／ELF／模型／分区、配置、输入快照及第三方许可保存在Git忽略的`evidence/waveshare-lcd-20261109/<profile>/`。没有复制到网关OTA路径。

SDK重新只读核对HEAD为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，NimBLE与idf_as_lib shell原有修改仍在，镜像自报5.5.6-dirty。当前触摸get_data调用已编译通过，最后一次新源码编译未再出现get_coordinates弃用告警；既有LVGL flag弃用告警仍在，不进行无关全库API重写。

当前188个官方变体中15个有源码、5个通过当前源码的缓存构建、10个有源码但未编译、173个无源码。本轮188项实机验收均未执行。清单中的5个成功只证明各自本地构建与镜像检查，不覆盖其他芯片、干净安装、声学效果或实际板子运行。全部构建已结束，没有安装、测试套件、类型检查、Git写操作、上传、发布、长期服务启动、刷机或OTA。

最终编辑审查实际执行`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp`，退出0；23份本轮相关未跟踪文件分别执行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。运行时、网关、control_protocol及自然插话文档的并发工作未编辑或回滚。
