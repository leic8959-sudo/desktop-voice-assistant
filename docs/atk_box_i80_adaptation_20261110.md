# ATK BOX的I80屏与运行时音频选择

2026-11-10，Asia/Shanghai。固定范围仍为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的154个发布目录／188个变体。本批新增 `alientek-atk-dnesp32s3-box` 基础终端源码候选，应用身份 `assistant_atkbox`，同一入口包含ES8311与数字音频两条运行时路径；它不是BOX0或ATK开发板的别名。

## 来源和硬件选择

来源为[固定ATK BOX目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/alientek/atk-dnesp32s3-box)的config.h／config.json／atk_dnesp32s3_box.cc，以及同提交no_audio_codec.cc。公开副本和逐文件URL／SHA256在忽略目录 `evidence/atkbox-public-20261110/`。未导入上游应用、面板驱动表或数字codec实现，本地仅按引脚、寄存器及实际传输格式实施。

固定板类先配置XL9555端口方向0x3B／0xFE，读取port0 bit5：高选ES8311，低选NS4168数字双工；之后方向改0x1B／0xFE。读取错误返回默认0会误选数字硬件，本地不会按I2C错误或codec未响应选择替代设备。

`atk_box_board.cpp` 在单线程启动时初始化I2C0上的XL9555地址0x20（SDA48／SCL45，100kHz），只改源码实际涉及的方向位：先使bit5输入，另设bit2／6／7与bit8为输出，连续三次核对bit5一致，采样之间10ms，再令bit5输出。每次I2C调用100ms超时，方向写后回读；保留所有输出锁存和无关方向位，成功并释放I2C device handle后才发布只读选型。读取失败、状态不一致、写入或清理失败均返回错误，选型保持Uninitialized。成功后不会在运行中切换codec或重新读取正被用作输出的bit5。

上游没有解释这些输出位各自的电源／PA／背光功能，本地没有套ATK开发板的bit2低有效功放、PCA9557的bit1高有效功放，或强行改写输出电平。方向配置与读取逻辑有固定来源，具体电气用途、启动锁存与真实硬件修订仍未验收。

## I80显示

| 项目 | 源码候选 |
| --- | --- |
| 总线 | 8bit I80、10MHz、队列10；WR42／DC2／CS1／RD41保持高，D0–7为40／39／38／12／11／10／9／46 |
| DMA | 最大320×20×2字节，64字节burst；沿现有20行双缓冲刷新，不以全屏153,600字节分配DMA链 |
| 面板 | ST7789，RGB565，RST=NC，标准reset／init后设置来源的COLMOD0x65，invert=true、gap0、swapXY=true、mirrorX=true／mirrorY=false |
| 界面 | 320×240横屏、16px中文；现有LVGL panel IO回调、RGB565字节交换和卡片渲染；BOOT0先释放再按住说话／卡片选择确认，无触摸声明 |
| 屏幕开关 | 通过panel display on/off实现可见／空白；偏好0关闭，非零映射打开。不是物理背光PWM，关闭显示不证明背光熄灭或节电 |

ATK BOX选择I80 IO创建函数，不初始化SPI总线或GPIO_NC PWM。I80 IO创建失败时销毁刚创建的bus并记录清理错误；不在成功后立即释放仍被panel IO引用的bus。默认原始MADCTL0x00与RGB初始缓存一致，随后驱动正常应用swap／mirror，不复制冗余写入以绕过驱动缓存。0x65是固定源码并口像素格式值，仍待真实面板颜色与字节序验收。

仅复用本机SDK的I80接口及LVGL适配层，没有复制厂家驱动。RD／WR时序、DMA对齐与20行刷新、COLMOD0x65、并口位序、320×240方向／文字／配对码／卡片目标均未实测。

## 两条音频路径

共同引脚：24kHz、I2S0，BCLK21／WS13／DOUT14／DIN47，MCLK=NC。只有选中的运行时分支创建I2S通道，不能同时占用I2S0。音频必须在板级选型成功之后初始化。

- ES8311分支：复用现有单codec单声道16bit引擎，I2C0 SDA48／SCL45，use_mclk=false，BCLK衍生内部时钟、PA=NC；输入输出同一handle，一次打开。模拟麦克风增益仅在此分支可用，待机30dB／播放12dB沿既有候选策略，非本板校准值。
- 数字分支：复用既有数字音频接口、显式传输超时和PCM静音，实现本板I2S0双工16bit stereo/BOTH slots。输入读完整双声道帧，选择左声道PCM16供单声道语音管道；输出将同一单声道PCM复制到左右槽。mic／speaker为独立handle，共享I2S0时钟，分别启停后核实际状态；不调用不支持的模拟增益API。

这里不能直接复制上游 `NoAudioCodec::Read`：该公共方法读取int32再右移12，但本板派生类实际配置16bit stereo，会把两个声道拼成一个32bit值。原Write同样使用int32分配／转换。当前本地按实际声明的16bit双槽格式作显式通道转换，**不会继承这个错位读取和四位软件增益**；因此其录音幅度、slot选择及扬声器双槽行为与旧软件处理有差异，必须实机确定。编译通过并不证明左槽就是该硬件麦克风或新幅度足够唤醒。

共用语音管道仍采样到16kHz并使用软件播放参考AEC。数字静音将实际输出PCM清零，ES8311使用寄存器静音；没有已核实的扩展口PA控制，不能宣称电气功放已关闭。没有硬件参考、波束成形或两种硬件声学验收证据。运行时microphone gain能力由已选硬件判断，其他板保留各自原能力值。

## 未完成范围

XL9555输出2／5／6／7／8的实际功能与有效电平、硬件型别标志、电源锁存、物理背光和PA关闭、扩展按键、LED4、原点击聊天／启动配网与电源手势仍未闭环。16MB Flash／Octal PSRAM沿固定默认，未实测。只有现有微雪1.8实物，屏控修订尚未确认；本板两种音频硬件、I80屏及同板OTA均无实机证据，全部188项实机状态保持not_run。

## 实际构建

新板实际调用：

```powershell
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:TMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board alientek-atk-dnesp32s3-box
```

使用已有SDK5.5.6-dirty／匹配工具／Python及17份本地组件override，Python外网守卫拒绝外网socket／DNS；SDK保留原NimBLE／idf_as_lib修改，override含其他BSP，不是干净发行依赖锁。BOX0、DuChatX、立创分别在原隔离目录运行临时 `xy_atkbox_regression_20261110.ps1 -Stage <目录> -Board <profile>`，按内容摘要同步并刷新变化文件时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，覆盖单codec／分离I2S数字／双codec路径及共用gain能力判断。

ATK BOX完整缓存构建和最终同目录 `reconfigure build` 均退出0；最终增量前刷新隔离目录main／audio／digital audio／display／board源的UTC时间，日志确认main、audio和digital audio实际重编。三个既有入口回归也均退出0，并确认共用main／audio重编，DuChatX另重编digital audio。没有重启仍在运行的构建，也未为归档重复全量构建。既有LVGL9.6弃用接口警告保留。

| profile | 应用字节 | 最小应用分区字节 | 剩余字节 |
| --- | --- | --- | --- |
| alientek-atk-dnesp32s3-box | 2,225,424 | 3,145,728 | 920,304（约29%） |
| alientek-atk-dnesp32s3-box0 | 2,245,760 | 3,145,728 | 899,968 |
| du-chatx | 2,207,152 | 3,145,728 | 938,576 |
| lckfb-lichuang-dev | 2,263,424 | 3,145,728 | 882,304 |

四款分别执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，均退出0，项目名、16MB配置、checksum及validation hash有效。模型均2,203,819字节，分区4,194,304字节，应用与模型均未超容量。新板另外实际执行匹配工具链的 `xtensa-esp32s3-elf-nm.exe -C build/assistant_atkbox.elf` 并筛选符号，退出0，最终ELF有 `atk_box_board_init`、`digital_audio_init`、`es8311_codec_new`、`esp_lcd_new_i80_bus`。这证明两条运行时音频实现和I80代码都被链接，不能证明物理选型、I2C／GPIO或声学成功。

一次性 `xy_record_atkbox_20261110.py` 退出0，核对每份89个源码／配置摘要、完整82文件main集合、合并defaults、实际选择源、生成配置及容量，并要求最终main／audio重编日志和新板符号证据。记录为 `cached_build_attempts_20261110_atkbox_<profile>.json`，日志、ELF／镜像、配置及源码副本位于忽略目录 `evidence/atkbox-builds-20261110/`。仅新板选中 `atk_box_board.cpp`，不把其他入口归档这份文件说成它们也编译了I80。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`，退出0。当前53个本地profile对应52个官方变体（另1历史兼容入口），136项尚无源码；累计31个官方变体曾分别缓存编译通过。按当前源码摘要，4项通过、27项历史通过证据过期、6项依赖预检失败、151项未构建（其中15项已有源码）。累计通过不是当前源码通过，全部188项实机状态仍为not_run。

实际执行跟踪修改的 `git diff --check -- firmware/assistant_espidf/main/audio_io.cpp firmware/assistant_espidf/main/display_ui.cpp firmware/assistant_espidf/main/CMakeLists.txt`，退出0；新增／未跟踪源码、配置、文档以 `git diff --no-index --check -- NUL <文件>` 空白审阅，返回1表示新增差异，未输出空白错误，保留LF／CRLF提示。未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、刷机／OTA、连接硬件验收、Git写入、上传或发布；保留网关／运行时及自然插话并发改动。
