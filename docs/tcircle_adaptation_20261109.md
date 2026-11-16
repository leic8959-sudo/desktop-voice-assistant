# T-Circle-S3 两个修订的板级适配

2026-11-13 后续更正：固定厂家资料确认 MAX98357A，已核对器件规格明确不支持24kHz。本地两款 T-Circle 的物理输出改为16kHz、stereo16复制mono，复用现有网络24→16kHz FIR及相同实际播放PCM的AEC参考；原版24kHz标准采集／V1.1 PDM32kHz采集保持。本文前段24kHz播放和独立音频文件的描述为历史快照，当前以源码为准；本轮构建与声学边界见[修正记录](remaining_s3_rebuild_20261113.md)。

最新B2进展见[微雪1.32／2.06／2.16源码记录](waveshare_b2_adaptation_20261109.md)：当前14个本地profile对应13个官方变体，175个仍待源码；新三板缺BSP未编译。以下保留本页对应阶段的数量和构建记录。

日期：2026-11-09，Asia/Shanghai。全量范围仍为小智 v2.5.2 的154个发布目录／188个变体。新增两个实际源码入口后，现有11个本地profile对应10个官方变体，另有1个历史CO5300兼容入口；178个官方变体仍待源码适配。源码、缓存构建、干净依赖构建和实机验收分别记录。

牛牛已确认目前只有现有微雪1.8可用于后续实机验收；未据此推断具体屏控修订。没有T-Circle实物，本轮不刷写微雪、不做OTA、不制造其他板型的实机记录。

## 两款入口

| profile | 应用身份 | 麦克风 | 扬声器 |
| --- | --- | --- | --- |
| `lilygo-t-circle-s3` | `assistant_tcircle` | I2S0，24kHz，32位左槽取高16位，MSM261 | I2S1，24kHz，MAX98357A |
| `lilygo-t-circle-s3-v11` | `assistant_tcircle11` | I2S0，PDM 32kHz，MP34DT05-A，FIR降至16kHz | I2S1，24kHz，MAX98357A |

V1.1的PDM时钟约2.048MHz；不能为了匹配唤醒模型直接降成16kHz。公共音频层分别打开采集和播放句柄，播放及软件参考仍以24kHz处理。数字麦克风没有ES8311／ES7210式的模拟输入增益，明确跳过该能力，不把不支持返回值吞成成功，也不向其写模拟codec寄存器。

`main/t_circle_audio.cpp`只为这两个profile编译：独立I2S控制器、固定格式校验、一个1秒截止时间内累计短读／短写、失败录音清零并返回错误。使用已存在的`esp_codec_dev1.5.11`软件音量及dummy codec管理PA GPIO45；两个profile默认打开`CONFIG_CODEC_DUMMY_SUPPORT`。该版本codec库会忽略data接口部分错误返回，因此应用在接受open成功前另核对两个通道的真实状态。

## 屏幕与输入

`main/t_circle_display.cpp`实现GC9D01N SPI3、40MHz、RGB565、160×160，以及GPIO18 PWM背光。CST816D使用I2C0、SDA11／SCL14、地址0x15，LVGL定期读取触点。读取失败或坐标越界时重置当前触摸按压，避免把通信失败作为按钮松手确认；错误和恢复仅在状态变化时记录。GPIO0仍按住说话。无SD，不调用其他BSP的SD挂载接口。

`display_layout.h`新增160×160布局；保留16px文字，缩小点云画布，状态／标题横向滚动，卡片详情放入独立的纵向滚动区域，操作按钮固定在圆屏内侧。配对页保留六位码及中文入口指引；小屏提醒按钮显示“稍后”，仍调用原有5分钟snooze操作。小屏路线卡使用可滚动文字，不绘制会遮盖详情的大导航箭头。实际可读性、滚动手感和触摸坐标仍待实机确认。

未接入旋钮、APA102、红外扩展板或其他专属功能。

## 来源与许可

硬件事实来自小智固定提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的两个板型目录。原厂固定提交`5a6f28748f2fb753c25816bfe156f3ba0155627e` README列出V1.0／V1.1均为16MB Flash／8MB Octal PSRAM，符合当前S3引擎容量配置；这仍不代替核对购买实物。

- [原版板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/lilygo/t-circle-s3)
- [V1.1板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/lilygo/t-circle-s3-v11)
- [原厂README](https://github.com/Xinyuan-LilyGO/T-Circle-S3/blob/5a6f28748f2fb753c25816bfe156f3ba0155627e/README.md)
- [嵌套TFT_eSPI初始化表](https://github.com/Xinyuan-LilyGO/T-Circle-S3/blob/5a6f28748f2fb753c25816bfe156f3ba0155627e/libraries/TFT_eSPI-2.5.43/TFT_Drivers/GC9D01N_Init.h)
- [同库许可文本](https://github.com/Xinyuan-LilyGO/T-Circle-S3/blob/5a6f28748f2fb753c25816bfe156f3ba0155627e/libraries/TFT_eSPI-2.5.43/license.txt)

仅将该嵌套库初始化命令重排为`main/t_circle_gc9d01n_init.h`中的46条表项；保留全部原始嵌套库许可和Bodmer版权通知于`main/third_party/tft_espi/`，分发二进制时也须携带该通知。原库声明原创代码采用FreeBSD条款，另保留Adafruit声明；不能把顶层GPL或小智顶层MIT随意套给不同文件。未复制Arduino应用或小智的无许可头panel glue，未改项目整体许可证。Bodmer上游仓库相同路径未找到，本文据原厂所带嵌套库和其同目录许可标注来源，不宣称已获得单独授权。

## 构建及剩余验收

两款各自完整缓存构建、最终源码增量构建均退出0，镜像只读检查的checksum／validation hash均有效，应用名各自正确。使用已安装IDF5.5.6-dirty、现有Python／GCC／CMake／Ninja及17个既有组件的独立副本，Python网络守卫拒绝外网。构建不是重新安装依赖，也不是干净发行依赖验收。

实际命令（在仓库根目录，两种`-Board`分别执行）：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board lilygo-t-circle-s3
# 第二次使用 -Board lilygo-t-circle-s3-v11
```

在各自隔离目录、同SDK／工具／网络守卫环境中同步最终音频与界面源码后，分别执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> build`，均退出0；再运行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<对应应用名>.bin`，均退出0。没有连接串口。

| 入口 | 最终应用字节 | 3MB应用分区剩余 | 本轮实机 |
| --- | --- | --- | --- |
| T-Circle原版 | 2,507,008 | 638,720（20%） | 未执行 |
| T-Circle V1.1 | 2,510,064 | 635,664（20%） | 未执行 |

两份`firmware/assistant_espidf/cached_build_attempts_20261109_lilygo-t-circle-s3*.json`记录38份源码／配置的摘要、实际命令、SDK身份、17个override、产物及日志摘要；记录时均与工作区匹配。原始日志、镜像、ELF、模型、分区、配置及第三方许可保存在Git忽略的`evidence/tcircle-firmware-builds-20261109/<profile>/`，没有复制到网关OTA路径。矩阵只记`passed_cached_sources`，不记为干净发行构建通过；后续编译输入发生变化时会变为`stale_cached_sources`。

首次原版构建出现I2S零值invert字段未显式初始化的告警，已修正并做最终增量构建；LVGL已有flag API弃用告警仍保留。新增PowerShell缓存包装入口本次已实际执行成功，但没有验证所有异常分支；Docker导出脚本补上T-Circle第三方许可随产物导出，本次未运行Docker导出。

源码与清单之外仍需：干净依赖锁、实际屏幕颜色／触摸／配对／提醒、麦克风通道与PCM对齐／PDM极性、功放静音和重开、唤醒／回声及同板OTA验收。现有微雪1.8的历史构建证据不能替代这些项目。当前未提交Git、上传、发布或操作设备。

## 同轮微雪1.8-v2回归构建与B2调查

额外以相同已有环境运行`tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board waveshare-amoled-1.8-v2`，完整构建退出0。只读`esptool image_info --version 2`退出0，应用身份`assistant_amoled18v2`、checksum及validation hash正确；应用2,614,688字节，3MB分区余531,040字节（17%）。38份输入与工作区匹配，记录为`cached_build_attempts_20261109_waveshare-amoled-1.8-v2.json`，证据放同一evidence根目录下对应profile。该结果用于核对公共代码的旧板路径，不推断牛牛实物就是v2，也没有重新构建或替换历史兼容镜像。

当前188个官方变体中10个有源码入口，3个通过各自的缓存构建，7个有源码但尚未构建，178个待源码，全部本轮实机验收未执行。历史兼容入口的早先构建单独保留，不重复计数。

B2先读取固定提交的微雪1.32／1.43C／2.06／2.16各自config和板类，差异写入`board_research_v2.5.2.json`并同步矩阵。其中1.32／1.43C的名称虽含touch，所读板类只有未使用的触摸变量，没有实际初始化／读取路径；这只能说明上游这部分实现不完整，不能据此宣称硬件无触摸。2.06使用SH8601／FT5x06，2.16为CO5300／CST9217且复位、音频、PMIC／扩展器不同。四项继续保持inventory_only，没有仅按名称或尺寸增加profile。

只读Git diff与新文件空白审查用于检查编辑内容；没有运行测试套件或独立类型检查。所有构建均已退出，没有启动长期服务、安装依赖、操作账号／家庭设备、Git写入、上传、发布、刷机或OTA。

最终编辑审查实际执行：`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp tools/build_firmware.ps1`退出0；25份未跟踪的本轮相关源码／配置／记录分别执行`git diff --no-index --check -- NUL <文件>`。24份无空白错误，原样保留的第三方`main/third_party/tft_espi/license.txt`第25行有上游自带的两个空格，该文件检查退出3；保留其原文字节，没有把所有空白检查写成通过。其余输出为Git的LF／CRLF提示。网关、运行时和control_protocol的并行改动未编辑或回滚。
