# CoreS3 本地板级接入与首次构建（2026-11-13）

本批推进已有 `m5stack-core-s3` 源码入口，应用身份保持 `assistant_cores3`。接入本地供电、显示、触摸、SD和AW88298输出，复用既有ES7210采集、软件AEC、自定义唤醒、触摸按住说话和双OTA；BOX-3另做共享codec回归。最终构建结果见文末，实机状态仍独立记录。

## 来源及前提修正

固定Espressif BSP3.0.2提交 `1bd663be5317bfc4457335011889eec5db33098d` 的14份原文按Git blob及SHA256校验。另只读取得M5Unified `96576a14247ddc935149145aa6b51943464120f1`、M5GFX `28d47949bdc84f30881623476f555398e2194313` 的5份对照源码，以及Adafruit AW9523 `43744cab9ed6e65e73964a04941673a7e823b18b` 两份寄存器对照。后7份只作事实比较，未导入、链接或安装库。

CoreS3确实将GPIO35同时用于LCD D/C和SD MISO。厂家M5GFX在LCD片选期间开启该脚输出，结束后关闭输出。本轮最初据接口形态判断当前SDK缺少这一步，进一步读取实际 `esp_lcd_panel_io_spi.c` 后发现判断不正确：现有SDK5.5.6已经在pre callback启用D/C输出、post callback关闭输出。其当前文件与SDK HEAD原文一致，限定路径Git状态为空；记录了源码、Git结果和摘要。**本批没有增加SDK补丁或新的总线实现。**

这个静态依据不等于共享总线实测。SDK的队列、总线获取与回收仍有 `portMAX_DELAY`，不能据I²C100ms时限推断显示／SD整个操作具有有限延迟。实际时序、并发及故障行为仍需CoreS3硬件验收。

厂家M5GFX还区分ILI9342C与后续ILI9342E，固定BSP使用名为ILI9341的工厂。本项目沿固定BSP的DCS参数路径，不能把工厂名当成实体芯片身份，也不宣称所有CoreS3／CoreS3-SE／新屏修订通用。

## 接入内容

| 模块 | 本地路径 |
| --- | --- |
| I²C | I²C1，SDA12／SCL11；AXP2101 0x34、AW9523 0x58、FT5x06 0x38及codec共享；本地每次I/O100ms |
| 音频电源 | ALDO1设1.8V供AW88298、ALDO2设3.3V供ES7210，仅修改电压低5位及所用使能位，保留其它轨和充电配置 |
| 扩展器 | P0_0触摸复位、P0_2 codec复位／使能、P0_4 SD使能、P1_1 LCD复位；先设低电平锁存，再设GPIO模式／输出方向并读回；非拥有输出位保留 |
| 显示 | 320×240 BGR／RGB565、反色；SPI3 mode0／40MHz，MOSI37／MISO35／CLK36、LCD CS3／DC35；复用SDK DCS操作和已有18条ILI9341默认表，保留两条F7 |
| 显示任务／内存 | LVGL core1、优先级2，双20行内部DMA条带共25,600字节，最大单次传输12,800字节；无换轴、无镜像 |
| 触摸 | FT5x06 1.1.1初始化；无直接GPIO reset／IRQ，板级AW负责reset；原始7字节count／event／坐标检查，初始及错误后等释放，多点／非法事件／越界取消按压 |
| 输出 | AW88298真实工厂、地址0x36（codec接口8位地址0x6C）、reset_pin=NC、硬件增益15dB；复位使能跨空闲保持，静音核对AW88298实际DAC callback |
| 输入／时钟 | ES7210 MIC1/MIC2标准双槽、选择第0槽；I²S0 24kHz，MCLK0／BCLK34／WS33／DOUT13／DIN14；播放PCM参考和AEC保留 |
| SD | 与LCD同SPI3，CS4、MOSI37／MISO35／CLK36；ALDO4 3.3V和P0_4，仅挂载现有卡，不格式化；缺卡沿存储错误路径，不阻止语音启动 |
| UI／协议 | GPIO0只作MCLK，不读取为按键；保留原触摸按住说话、卡片、绑定、HTTP／WebSocket、custom wake“你好，小燕子”、自然插话调度、媒体和双OTA |

AW9523的0x11 bit4是整个P0的推挽属性，沿固定BSP要求设置；其余位保持。模式位1代表GPIO、方向位0代表输出，另与对照驱动的实际位操作核对。寄存器读回只证明所读锁存／配置值，不证明实体电平或电源轨已测量。

原BSP的speaker初始化同时设置ALDO3，注释为codec／mic；厂家M5Unified将ALDO3明确写作camera。此实现按厂家电源用途只设置音频所需ALDO1／2，不重写或使能相机ALDO3；也不复制BSP显示初始化顺带开启camera的行为。相机、IMU／RTC、电池、充电、关机按键、睡眠和原厂MCP没有接入，不能由原BSP的能力宏推断支持。

背光由DLDO1控制，正亮度沿固定BSP公式写0x99低5位20..28，再按位启用0x90 bit7；亮度0则真正关闭该位，不把最低电压当熄屏。显示失败尝试关闭背光后停止启动，已创建但可能被回调引用的资源保留，要求重启。没有切断后台MCU或音频电源。

音频启动先释放P0_2一次，之后由AW88298数字静音管理输出，不在每次静音时复位codec。GPIO0不会传入codec reset或按键路径。checked数据接口继续传播底层格式／启停结果；AW88298支持24kHz的分支已从本机真实驱动读取，但不代表时钟、声音、增益或声学已验收。

## 改动范围与构建

新增 `cores3_board.h/.cpp` 与Apache许可／来源通知，共4份main文件；修改5份既有main：board_profile、CMake、main失败处理、audio_io的CoreS3专用入口和共享codec的AW88298选择。CoreS3 manifest以现有FT5x06精确1.1.1替换未安装BSP，默认配置显式开启AW88298，registry只改本板BSP选择及实现说明。导出工具仅补充第三方通知目录，未执行导出／Docker。

控制协议人工决定、唤醒、配网绑定、NVS／凭据、OTA保护、存储及原触摸按住说话UI保持本批前内容。SDK无修改；SPI问题是在核对中排除了错误前提，没有安装新的库或工具链。

CoreS3直接依赖预检退出0，未创建预检目录。新隔离stage为 `I:/xy_board_builds_20261109/cores3_native_20261113`，BOX3复用其独立stage同步当前源码。现有SDK5.5.6-dirty、Xtensa14.2.0、17份本地override和offline_guard，各2个编译任务。

```text
python -B evidence/cores3-native-20261113/cores3/build_cached.py
python -B evidence/cores3-native-20261113/box3/build_cached.py
python -B evidence/cores3-native-20261113/review_sources.py
python -B evidence/cores3-native-20261113/cores3/collect_build.py
python -B evidence/cores3-native-20261113/box3/collect_build.py
python -B evidence/cores3-native-20261113/finalize.py --record
python -B tools/sync_board_matrix.py --source-directory evidence/electron-source-20261111/inventory
python -B evidence/cores3-native-20261113/finalize.py
```

初始查询M5Unified旧 `Power_Class.cpp` 路径时未找到，读取已取得的完整tree后改用当前 `Power_Class.inl` 成功。没有据旧路径缺失判断功能不存在。所有对照源均固定提交并核对内容；来源只读，不执行其代码。

只有现有微雪1.8实物。CoreS3 PCB／Flash／QUAD PSRAM／屏控修订、AW复位／其它扩展器输出、PMIC电源轨、DLDO1亮度、FT坐标／取消、触摸说话、SD与LCD GPIO35并发、AW88298真实静音／增益／时钟、MIC槽、唤醒／AEC／自然插话、媒体、绑定提醒、HTTPS OTA／回滚，以及内部DMA／堆／栈峰值仍需实机验收。构建及符号存在不能替代这些证据。

本批没有执行测试套件、类型检查、模型推理、长期服务、Git写操作、安装、刷机、OTA、上传或发布。全量适配目标继续进行。

## 最终结果

CoreS3第一次完整 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=m5stack-core-s3 build` 退出0；BOX3的独立profile执行 `reconfigure build` 退出0。两板各八项readelf／nm／size、esptool镜像校验、分区解析和SDK只读查询均退出0。

| 实际目标 | 应用字节／3MiB余量 | 模型字节 | 当前输入／产物 |
| --- | ---: | ---: | ---: |
| CoreS3首次 | 2,604,496／541,232 | 2,203,819 | 318／26 |
| BOX3共享回归 | 2,617,312／528,416 | 2,203,819 | 318／28 |

CoreS3 ELF包含AW88298真实工厂、ES7210、FT5x06、板级音频准备、SD挂载、SDK LCD SPI pre／post回调、custom wake／AEC、控制协议、设备HTTP／WebSocket和播放入口；不含ES8311工厂、BOX3 PA或TT21100路径。实际 `cores3_board.cpp` 编译命令选中CORES3宏，sdkconfig为16MB Flash、QUAD PSRAM、AW88298和mn5q8默认模型；BOX3继续其Octal与ES8311选择。

20,496字节软件播放参考仍存在。实际分区二进制保留NVS、双3MiB应用及4MiB模型，总范围不超16MB。每份stage与318项当前工作区输入摘要匹配，54份产物摘要已核对；没有以此前BOX3产物代替共享代码变化后的结果。

两份ELF均记录既有SDK强制UND名称 `__cxx_fatal_exception`、`esp_system_include_coredump_init`、`start_app`、`start_app_other_cores`。SDK仍为5.5.6-dirty，其SPI源本身与HEAD一致；编译链接和静态源码核对不证明启动、干净发行或实际SPI波形。LVGL及既有BSP配置警告保留在日志。

直接依赖报告命令实际退出0：926条声明、111本地／110官方入口满足直接条件；这是本地条件结果，不等于所有板型依赖解析完成。最终来源、范围／保护文件、当前输入与产物摘要核对和矩阵离线同步均退出0。`git diff --check`退出0，改动／新增main的整文件`--no-index --check`返回1且无空白诊断；原始第三方LICENSE保持原文。

全量仍为154目录／188变体、152个官方源码入口、153本地profile、36项无源码、4项明确必需功能缺口。累计 **110个官方变体曾缓存构建通过**；当前 **2 passed_cached_sources／108 stale_cached_sources／78 not_run**。188项本轮实机验收均not_run，尚不能宣称全部适配完成。
