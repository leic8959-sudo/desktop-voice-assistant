# 微雪1.85／1.85B／1.85C源码适配

后续更新：微雪1.85／B／C已分别完成首次整板缓存构建，保留216默认／181B版／184V2选表及复位差异；VoCat直接复位回归通过。实机仍未验收。见[本批说明](ws185_qspi_native_20261112.md)。

最新TFT进展见[本轮记录](tft154_adaptation_20261109.md)：25个本地profile对应24个官方变体，164个待源码，10个当前缓存构建通过；以下保留本页对应阶段的数量和证据。

2026-11-09，Asia/Shanghai。全量基线仍为小智v2.5.2的154个发布目录／188个变体，本轮新增三份实际源码入口。当前21个本地profile对应20个官方变体（另有1个历史兼容入口），168个官方变体仍待源码。新三板本机缺ST77916组件，没有编译或实机验收；不会把仅增加入口当作全量完成。

## 三个型号的区别

| profile／应用名 | 音频与输入 | 显示／触摸／供电 |
| --- | --- | --- |
| `waveshare-lcd-1.85`／`assistant_lcd185` | 数字I2S16kHz右槽麦克风、24kHz双槽PCM5101，32位承载；GPIO0按住说话 | 360×360 ST77916／CST816，TCA9554 P0/P1复位；主I2C0 SDA11/SCL10，独立触摸I2C1 SDA1/SCL3，IRQ4；PWR6新按住5秒释放保持GPIO7 |
| `waveshare-lcd-1.85b`／`assistant_lcd185b` | ES7210输入＋ES8311输出，24kHz，PA9，GPIO0按住说话 | 360×360 ST77916／CST816S；LCD复位GPIO3、触摸复位GPIO1；共享I2C0 SDA11/SCL10，IRQ4；不初始化不存在的复位扩展器 |
| `waveshare-lcd-1.85c`／`assistant_lcd185c` | 明确使用上游全局Kconfig默认VERSION_2_0，ES7210／ES8311，24kHz、PA15 | 同360屏和扩展器复位；触摸与codec共享SDA11/SCL10，不在相同引脚另建第二个I2C控制器；1.0数字音频硬件另待移植 |

三款资料均为16MB Flash／8MB PSRAM，使用现有16MB分区、Octal PSRAM和独立应用身份。B/C的I2S为MCLK2、BCLK48、WS38、输入39、输出47；原版数字麦克风为BCLK15／WS2／DATA39，扬声器BCLK48／WS38／DATA47。共享数字后端继续提供实际PCM静音和短读检查；B/C复用已经实现的codec数据格式／启停错误检查，选择MIC1进入公共单声道／软件参考流程，不宣称波束成形或硬件AEC通过。

固定小智提交为`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。config.json、config.h、板类和全局Kconfig联合核对，不能因为1.85C的sdkconfig_append为空就误称未选硬件版本；其choice明确default VERSION_2_0。厂家1.85B独立产品页确认16MB／8MB和CST816S，1.85C Wiki的触摸／LCD复位与共享总线被单独核对。

- [原版固定板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.85)
- [B版固定板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.85b)
- [C版固定板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/waveshare/esp32-s3-touch-lcd-1.85c)
- [固定全局Kconfig](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/Kconfig.projbuild)
- [1.85B厂家独立产品页](https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-1.85B.htm)
- [1.85C厂家Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85C)

SD、RGB灯、传感器、RTC、电池遥测等未接入当前语音固件；这不表示硬件不存在。storage未启用时的日志改为“当前固件未启用SD缓存”，避免把尚未实现错误描述成整板无SD。

## 屏幕ID与初始化表

固定源码以低速读取0x04寄存器的4字节结果识别两种面板：

| ID | 原版／C版 | B版 |
| --- | --- | --- |
| `00:7F:7F:7F` | 已固定的ST77916组件默认表 | 181条B版专用version1命令 |
| `00:02:7F:7F` | 184条version2命令 | 同一184条version2命令 |
| 读取失败或其他ID | 初始化明确失败 | 初始化明确失败 |

原版／C版旧代码在失败读取后仍可能查看未初始化数组，又在不删除原IO时重新注册80MHz设备。本地`waveshare_qspi_display.cpp`将ID缓冲清零，在3MHz设备完成读取后先删除该设备，分别检查读错／清理错误，再识别ID和创建80MHz正式设备；未知ID记录确切字节并返回不支持，不静默选表。没有照搬不安全的IO替换逻辑。

`st77916_board_init.h`把固定B版的两张表转换为C++静态字节存储，命令、有效参数长度、顺序和延迟不变。version2与固定原版／C版逐项比较一致；B版version1与Espressif当前默认表不同，必须保留其181条专用数据。原版／C版的version1沿用固定依赖2.0.2自身默认表；上游main manifest声明`^2.0.2`，本工程直接固定为2.0.2。

ST77916注册表2.0.2对应Espressif源码提交`91aeb7fb41e8a3e76aeb21371f9f83711c74cf3f`，其SPI驱动和API为Apache-2.0。自有代码只调用组件API，不安装／复制整套驱动。仅纳入小智板文件的初始化数据，并保留固定仓库MIT全文和版权通知于`main/third_party/xiaozhi_st77916/`；所读三个板目录没有单独覆盖许可证，文件也无其他许可头。该处理不能推广到带独立GPL声明的其他板。没有选择或修改项目整体许可证。

Docker产物导出已补充此许可目录随1.85固件导出，本轮没有运行Docker导出；依赖／来源记录和实际源码不同于已发布二进制证明。

## 360圆屏与触摸

共用QSPI显示连接：SPI2、CLK40、CS21、DATA46/45/42/41，mode0；ID读取3MHz，正式传输80MHz，32位命令、8位参数、RGB565。LVGL使用20行DMA双缓冲、SPI完成通知和偶数横向区域对齐，GPIO5 PWM背光。原版／C版保持扩展器高／低／高复位，B版使用直接GPIO复位。触摸轮询采用已存在的CST816S组件；失败或越界取消当前按压，防止变成卡片确认。

公共布局新增360×360圆形安全区，按钮和配对指引保持16px字体。原版沿用16kHz直通采集／24kHz播放参考；B/C保留输入模拟增益与独立输出音量。新三板的显示速度、触摸修订、音频通道、功放和PWR均仍待对应实机，旧入口构建不能替代这些验收。

## 额外容量更正

读取1.85B厂家资料时，比较表提示旧AMOLED1.75C可能是32MB；随后单独读取[1.75C自己的厂家产品页](https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.75C.htm)，明确写外接32MB Flash／8MB PSRAM。因此把`waveshare-amoled-1.75c`登记和sdkconfig默认容量由先前假设的16MB修正为32MB，保留原分区偏移；后续备份与烧录工具依据登记校验整片32MB范围。该入口没有本轮编译或刷机，也没有凭交叉商品表直接修改未核对型号。

## 已执行命令与限制

新三板分别实际执行：

```text
<已有IDF Python> -B tools/prepare_cached_firmware_build.py --profile <waveshare-lcd-1.85 / 1.85b / 1.85c> --destination <各自新临时目录> --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

三次都退出1，缺少已安装的`espressif/esp_lcd_st77916`，没有创建目标目录、启动编译或安装依赖。`native_dependency_preflight_20261109_st77916.json`保存完整命令、退出码、日志摘要；日志在Git忽略的`evidence/waveshare-st77916-20261109/`。依赖须由牛牛准备，不能把只读API资料副本当成已安装组件。

同时在六份既有隔离目录使用已安装IDF5.5.6-dirty和17个本地组件override，分别执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<各自profile> reconfigure build`，核对公共源码回归。最终源文件、镜像及检查结果另附下方；缓存副本不等于干净发行依赖锁。

牛牛只有现有微雪1.8实物，所有新增板均未做本轮实机验收。没有运行测试套件、独立类型检查、安装依赖、启动长期服务、Git写操作、上传、发布、刷机或OTA。全量目标继续，仍须全部源码、独立构建及实机逐项闭环。

初始化数据复核记录：初次转换把B版version1的注释项也计入，出现214条与实际181条不一致；尚未编译该板即发现并修正。去除注释后，181／184条有效命令与本地静态表的命令、参数、长度和延迟逐项相同。初次不一致和最终一致记录分别保存在evidence目录的`initialization-data-review-initial-mismatch.json`／`initialization-data-review.json`，没有将未通过的初次比对删除或写成成功。

## 最终旧入口回归证据

六个已有profile各自的重新配置／构建和最终增量构建均退出0，实际命令分别为`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`和相同参数的`build`。每份应用另执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<对应应用名>.bin`，均退出0，应用身份／Flash容量正确、checksum与validation hash有效。

| 已有profile | 应用字节 | 实际应用分区剩余 |
| --- | --- | --- |
| waveshare-lcd-0.85 | 2,538,400 | 214,112（8MB板，单应用0x2A0000） |
| lilygo-t-circle-s3 | 2,507,408 | 638,320 |
| lilygo-t-circle-s3-v11 | 2,510,400 | 635,328 |
| waveshare-amoled-1.8-v2 | 2,614,816 | 530,912 |
| waveshare-lcd-1.54 | 2,546,112 | 599,616 |
| waveshare-lcd-1.83 | 2,615,552 | 530,176 |

六份记录比对49份固件源码／配置及许可，记录时均与工作区一致；0.85使用它自己的8MB分区文件，其他五份使用各自实际输入。新ST77916编译单元和初始化表虽然在源码快照中，当前六个profile并不编译它们，所以这些成功结果不能当作新三板或1.75C32MB分支的编译证明。

最新`cached_build_attempts_20261109_st77916_<profile>.json`保存实际命令、SDK、49份摘要及镜像证据，原日志、应用／ELF／模型／分区、输入快照、许可和脚本在Git忽略的`evidence/waveshare-st77916-20261109/`。没有复制到网关OTA路径或改写设备。SDK只读核对HEAD仍为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，原NimBLE／idf_as_lib改动保留，镜像自报5.5.6-dirty；已有LVGL flag API弃用告警仍在。

当前20个官方变体有源码，其中6个当前缓存构建通过、14个已有源码未编译；168个尚无源码；188项本轮实机验收全部未执行。三个新增项的dependency_resolution为blocked_missing_local_sources、build为not_run。源码、干净依赖、各自编译和实机仍是不同阶段，目标保持进行中。

最终编辑审查实际执行`git diff --check -- firmware/assistant_espidf/main/CMakeLists.txt firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/storage.cpp tools/build_firmware.ps1`，退出0；34份本轮相关未跟踪文件各执行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。6份最新passed_cached_sources记录都有当前输入摘要匹配的证据。最终未使用include清理后再次build退出0，应用／ELF哈希与已完成镜像检查的产物一致。运行时、网关和control_protocol的并发修改未编辑或回滚。
