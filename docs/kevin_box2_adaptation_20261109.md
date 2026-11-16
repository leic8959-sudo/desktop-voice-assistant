# Kevin Box-2双I2C／TDM音频基础接入

2026-11-09，Asia/Shanghai。继续固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。新增本地基础入口后，38个本地profile对应37个官方变体，另有一个历史微雪兼容名；151项仍无本地实现。Kevin的蜂窝、充电管理与硬件参考未接入，不能把基础Wi-Fi源码记成整板完成。

## 固定来源与独立身份

profile=`kevin-box-2`，应用身份=`assistant_kevinbox2`，Flash16MB／Octal PSRAM沿固定默认配置，未测量实物。读取[固定Kevin目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/kevin/box-2)的config.h／config.json／kevin_box_board.cc，以及固定`main/audio/codecs/box_audio_codec.cc/.h`、`main/boards/common/axp2101.cc`。公开源码副本保留在忽略目录`evidence/kevinbox2-public-20261109/`，调查URL及未完成能力登记于`board_research_v2.5.2.json`。

## 双I2C与OLED

128×64 SSD1306位于I2C0：SDA7／SCL8、0x3C／400kHz、无X／Y镜像；与前几款OLED的SDA41／SCL42及镜像不同。`oled_display.cpp`仅在Kevin宏下使用此接法，继续复用现有文本／卡片／配对码、全帧单色转换和显式1秒传输超时。

codec与AXP2101位于独立I2C1：SDA42／SCL41。新增薄的`bsp_codec_i2c_init/get_handle`保留总线所有权，`bsp_i2c_get_handle`仍代表OLED总线。音频与PMIC调用明确使用codec总线，不将同一组数值误当成OLED总线，也不在两处重复创建I2C1。

BOOT0与音量上1／下2沿通用按键逻辑：启动先等全部松开，多键／跨键进入取消并等待释放；根页按住说话，卡片短按选择／长按松手确认。本机音量继续使用当前对话音量，不改提醒静音／音量，设置为当前进程状态。上游双击切换网络、长按直接0／100等手势没有接入。

## ES7210四时隙输入与ES8311输出

24kHz双向，MCLK40／BCLK38／WS47／输入39／输出48／PA9。复用现有`waveshare_spi_audio.cpp`的ES7210＋ES8311创建、显式超时I2S传输及底层set_fmt／enable错误记录；该文件按板级别名选择引脚及I2C端口，没有复制第二份双codec引擎。旧微雪入口继续原引脚／I2C0和双声道输入。

固定BoxAudioCodec实际使能ES7210的MIC1／2／3／4，使用16bit四时隙TDM接收；不能依据config中的“reference=true”就套双声道I2S。Kevin宏下接收初始化TDM4，打开输入时声明物理channel=4、channel_mask只选slot0／MIC1。原参考顺序与物理麦克风编号不同，当前没有使用硬件参考，不宣称双麦／波束成形验收。采集MIC1再经过已有24→16kHz处理和软件AEC。

ES8311输出仍是16bit单声道数据、STD模式。打开输入后显式重置输出channel=1／mask=0，避免将四个输入通道参数直接传给扬声器。初始TX即使用两个32位时隙／WS宽32，使每帧64个时钟与RX的四个16位时隙一致，避免初始化时两侧帧长不同。现有esp_codec_dev1.5.11会在混合STD／TDM共享时钟时协调时隙宽度；其底层配置返回值由既有checked-data接口记录并在打开后检查。TDM槽顺序、共享时钟／WS宽度、数据对齐、PA9、实际codec开启及AEC时序均无实机证据，编译成功不能证明运行时初始化成功。

共用静音修正沿用上一批：当前通道用户静音或临时暂停决定输出，静音／音量0时物理PCM和软件参考均置零，不在首包或恢复时擅自解除静音。此次未更改自然插话协议／唤醒人工决策，也未将原设备校准当作Kevin实测。

## 供电与未完成功能

复用`board_power.cpp`既有AXP2101有界读写／读回流程：Kevin使用I2C1地址0x34，确认0x93为28（3.3V），保留0x90其他位并置bit1使能ALDO2，最后读回确认；单次事务100ms超时，移除临时设备句柄并传播失败。微雪仍选择各自的ALDO1／ALDO2配置，不套Kevin的寄存器地址。

这只是原板必要初始供电的一部分。没有写入原Pmic中的充电电流／终止电流／电池电压／输入电流限额、温度检测、长按关机或电源阈值配置；它们仍由现有PMIC状态决定。没有调用上游GPIO4的ML307启用，也没有蜂窝网络驱动、Wi-Fi／4G切换、硬件回声参考、充电／电池遥测、软件关机／自动深睡或LED实现。完整适配依然需要补足这些能力及对应电路／实机证据，不能把缺失项目从全量目标删除。

牛牛只有现有微雪1.8；Kevin实物缺失。需核对Flash／PSRAM、双I2C、ALDO2读回与实际电压、MIC1与参考通道、混合STD／TDM时钟、静音／音量／PA9、OLED方向／卡片按键、配网／提醒／OTA，以及蜂窝／充电管理的独立实施与验收。当前未连接或操作设备。

## 实际构建与状态边界

本轮执行下列隔离缓存构建，不安装依赖：

```powershell
$env:TMP = 'I:\xy_board_builds_20261109'
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board kevin-box-2
```

使用已安装SDK5.5.6-dirty、17个既有组件本地override、Python外网守卫。HU-087、微雪LCD1.83、AMOLED1.8-v2分别在各自旧隔离目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`回归公共显示／音频／电源修改。最终退出、镜像身份、容量及源码摘要在本批收尾时补充；中间结果不能当作最终当前源码通过。

未运行测试套件／独立类型检查，没有安装、改SDK、刷机／OTA、启动服务、Git写入、上传或发布。网关／运行时并发改动保留。全部188项实机验收保持未执行。


## 最终实际构建／镜像结果

Kevin完整缓存构建及最终`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=kevin-box-2 reconfigure build`均退出0。最终增量构建纳入初始TX64-clocks/frame修正；三款旧入口在末尾源码修订后再次reconfigure/build，均退出0，初次回归日志另存。没有把早期成功日志替代最终源码构建。

四款分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份、16MB设置、checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| kevin-box-2 | 2,188,544 | 957,184（约30%） |
| hu-087 | 2,155,264 | 990,464 |
| waveshare-amoled-1.8-v2 | 2,615,520 | 530,208 |
| waveshare-lcd-1.83 | 2,329,104 | 816,624 |

每份记录有61个源码／配置摘要及完整main文件集合与当前工作区一致；Kevin最终配置确实启用ES7210／ES8311。记录为`cached_build_attempts_20261109_kevinbox2_<profile>.json`，日志、镜像、配置和源码副本在忽略目录`evidence/kevinbox2-builds-20261109/`。SDK仍保留原NimBLE／idf_as_lib修改并自报5.5.6-dirty，17个override包含非当前BSP，既有LVGL弃用警告保留；这不是干净发行依赖锁、全量宏分支或设备运行证据。

当前37个官方变体有源码，4项当前源码缓存构建通过，15项历史通过因公共文件变化为stale，3项GC9A01预检失败，15项仍未构建。累计19个官方变体曾各自编译通过；151项仍无源码，全部188项实机验收仍为not_run。Kevin的源码／编译状态不表示蜂窝、充电管理、硬件参考或整板交付完成。

实际执行`git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/CMakeLists.txt`，并对6个新增／未跟踪源码及文档执行`git diff --no-index --check -- NUL <文件>`审阅，无空白错误，保留Git的LF／CRLF提示；执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`更新矩阵。没有运行测试套件、类型检查、设备测试或刷机。
