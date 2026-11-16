# Korvo2 V3：默认ST7789与ADC按键

日期：2026-11-10，Asia/Shanghai。变体 `espressif-esp32s3-korvo2-v3`，独立应用身份 `assistant_korvo2`。实现固定发布配置默认的ST7789基础Wi-Fi语音入口，尚无实机验收；ILI9341和RNDIS不由本入口宣称支持。

## 来源与显示范围

小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的实际板类、config.h／config.json和Kconfig：默认 `ESP32S3_KORVO2_V3_LCD_ST7789`，280×240、RGB、反色、swapXY、mirrorY、gap20／0。SPI3 mode0／60MHz，MOSI0／CLK1／DC2／CS46，LCD硬复位由扩展器处理，ESP面板reset脚设NC。

官方esp-bsp提交 `93460b932beba7022a8a4ec4186e0d6b7533d05f` 的Korvo2 BSP用于核对扩展器映射、背光和ADC按键事实；其主要显示方案是320×240 ILI9341，不能用它替代固定小智默认ST7789。本地不导入整个BSP，不安装ILI9341或其他新组件。

| 部分 | 本地接入 |
| --- | --- |
| 显示 | 280×240 ST7789，独立布局、16px中文字体，面板和LVGL保持同一方向 |
| 扩展器 | I2C1 SDA17／SCL18；仅探测0x20，明确NACK时再探测0x38；不全总线扫描 |
| 映射 | 官方虚拟GPIO50／51从GPIO_NUM_MAX=49起，分别是P1背光、P2复位；P3是LCD使能，不将P0误当背光 |
| 启动 | P1背光先暗、P3先高；锁存后配置P1／P2／P3输出；P2高300ms、低300ms、再释放，P3拉低使能；保留P0及其他位 |
| 背光 | 扩展器仅开关，百分比0关闭、1–100打开；不宣称支持物理PWM调光 |
| 音频 | ES8311＋ES7210，I2C1；I2S0 MCLK16／BCLK9／WS45／DIN10／DOUT8；输入输出24kHz |
| 功放 | GPIO48高有效，先锁存低后输出；静音／关闭先关PA，实际DAC取消静音成功才使能 |
| 按键 | ADC1 CH4／GPIO5电阻梯，经曲线校准转mV读取；不对GPIO5配置数字BOOT输入或上拉 |
| 容量 | 固定release默认16MB／Octal，实际模组容量待确认 |

复用项目已有 `update_i2c_register_bits` 做限时读改写和读回校验，保留非目标位；没有另建TCA9554驱动。输出寄存器0x01、方向0x03与本机既有TCA9554组件一致。传输／探测显式100ms超时，亮灭更新受板级锁保护。启动失败记录原因、切断PA并停止本次启动，不自动擦配置或建立panic重启循环。

## 音频与按键语义

实际BoxAudioCodec启用四个ADC并使用TDM4输入；本地复用既有TDM4／64时钟帧的受检查双codec路径，启用四个物理MIC、读取slot0／MIC1为单声道，使用已有重采样和软件AEC。没有宣称移植原厂两路硬件参考输入AEC、波束成形或双击切换AEC。实际data set_fmt／enable、MIC1增益和DAC mute回调结果均按既有路径检查。

按键阈值来自固定板类及官方BSP一致的窗口：REC2310–2510mV、音量加280–480mV、音量减720–920mV。初始必须释放，读ADC或校准失败取消；按住时跨到另一动作窗口取消并等待释放。大于最高窗口2510mV用作本源码候选的释放区域，真实空闲电压、边沿抖动和窗口裕量需实物测量。电阻梯不能保证辨认多键组合，因此不宣称实现数字键式多键检测。

MUTE／PLAY／SET窗口和其他间隙返回Invalid取消；没有给这些键附加卡片确认、重配网或静音动作。原厂播放键本身是TODO；原厂SET重配网、MUTE切换、REC点击切换聊天、音量长按极值等没有完整移植，本地REC接已有按住说话和操作卡、音量键接已有调整与连按逻辑。校准失败停止启动，不用raw ADC计数假装毫伏。

默认ST7789入口不创建LVGL触摸pointer；相机复位／拍照、ILI9341与其触摸选项、SD缓存、电池／充电、休眠、LED和USB RNDIS均未实现。本轮不动这些外围。屏幕模块接线、扩展器映射／复位、实际容量、ADC校准／按键窗口、24kHz音频时钟／slot0、PA极性／静音／重开、增益／AEC／唤醒、联网／配对／操作卡及同板OTA均待实机验收。当前只有既有微雪1.8实物，屏控修订未确认。

## AtomS3＋Echo Base的前置项

同批核对 `m5stack-atoms3-echo-base`：固定config明确8MB Flash且 `CONFIG_SPIRAM=n`，README要求关闭唤醒与音频处理。实际板类也有AtomS3专用SPI接线／GPIO16背光，与带PSRAM的AtomS3R／LP5562路径不同。

小燕子当前display_ui的点云与双缓冲、audio_io的AFE帧和任务栈、control_protocol的音频队列／唤醒预缓冲和任务栈，以及main的分配策略依赖PSRAM。必须先完成有界内部RAM预算、手动录放音和对应UI的完整运行模式，才能接入这个板型；仅关闭PSRAM或借用AtomS3R配置会在运行时失败。矩阵保留inventory_only，未新增虚假的源码入口。实际板类与资源使用前置项已更新到原有research finding；没有修改并发的自然插话实现来凑支持数量。

公开固定源文件及摘要位于 `board_research_v2.5.2.json`，原文归档于忽略目录 `evidence/korvo2-source-20261110/`。本地只依据寄存器／接线事实编排既有驱动接口，没有复制上游板类应用或修改项目整体许可。

## 实际构建与结果

新板完整缓存构建命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board espressif-esp32s3-korvo2-v3
```

退出0，隔离stage为 `I:\xy_board_builds_20261109\xy_cached_5132e9a3c19a`。已有立创双codec／FT触摸／PCA9557回归执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lckfb-lichuang-dev reconfigure build`，也退出0。

首次日志有本板多声明未使用PWM状态变量的提示，已使该声明与实际PWM编译条件一致。保留首次日志与新板首次display源后，同步最终源／刷新变化文件时间，两profile再各自实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0，日志实际重编waveshare_spi_display且不再有该未使用变量提示。原有LVGL弃用提示保留。最终记录以最终源码为准，没有拿清理前镜像冒充最终镜像。

使用本机IDF5.5.6-dirty及匹配工具／Python，只复制17个既有组件override，保留SDK原有修改；Python offline_guard拒绝外网。没有安装依赖，不是干净发行依赖构建。

| Profile | 最终应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| espressif-esp32s3-korvo2-v3 | 2,260,112 | 885,616 |
| lckfb-lichuang-dev | 2,263,424 | 882,304 |

构建结束后，两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 及 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0；独立项目名／16MB／checksum／validation hash匹配。新板实际链接korvo2_board_init、read_keys、set_backlight、ADC曲线校准／毫伏转换、ES8311／ES7210及TDM初始化；旧立创仍链接其PCA9557／FT触摸路径，没有误选新板helper。

两份126项输入摘要／119个main文件集合、生成Flash／Octal／双codec配置、编译宏、合并defaults和工作区匹配。模型均2,203,819字节，在4MB模型分区内。首次／最终日志、初次display源、新产物／ELF／编译命令／配置／依赖锁／源快照及摘要见 `cached_build_attempts_20261110_korvo2_<profile>.json` 和忽略目录 `evidence/korvo2-builds-20261110/`。编译不证明扩展器接线、模拟窗口或声学表现。

实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前74官方源码变体／75本地profile含1历史入口／114待源码；累计45变体曾各自缓存编译通过。当前2 passed／43 stale／6历史依赖预检失败／137 not_run，其中23项not_run已有源码，全部188实机not_run。全量目标仍进行中。

本轮未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、执行Git写入、上传、发布、刷机或OTA。无屏手机热点读取六位绑定码的人工决策函数及并发的自然插话／网关／运行时代码未修改。

收尾实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。最终两份各126输入／18与17项构建归档产物、13项固定公开源证据摘要匹配；矩阵研究摘要和新板passed／实机not_run状态匹配。对照本批编辑前快照审阅最终diff；两个构建进程均已结束。
