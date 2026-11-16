# DFRobot S3 AI CAM 无屏语音适配（2026-11-10）

新增发布变体 `dfrobot-df-s3-ai-cam`，独立应用 `assistant_dfaicam`。固定参照小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的实际板类／配置／NoAudioCodec，以及厂家DFR1154 Wiki。只有现有微雪1.8实物，本板无实机验收；公开清单154目录／188变体。

## 实际资源与音频

厂家资料确认16MB Flash／8MB PSRAM，固定发布显式Octal。无屏幕，板载PDM麦克风和扬声器；使用现有16MB分区、4MB模型分区。没有按相似相机板改为8MB或添加屏幕。

| 资源 | 接入 |
| --- | --- |
| 麦克风 | I2S0 PDM，CLK38／DIN39，16kHz／mono LEFT／PCM16 |
| 扬声器 | I2S1标准I2S，BCLK45／WS46／DOUT42，24kHz／mono LEFT／32位槽 |
| 功放／增益 | 上游GAIN41只有宏定义、未用于实际构造；本地不驱动41，没有假设PA使能GPIO |
| LED | GPIO3，固定GpioLed构造output_invert=0，低电平初始关闭，高电平亮 |
| 按键 | BOOT0低有效，保留为手机查看窗口手势；语音使用唤醒词 |
| Wi-Fi | PHY最大发射功率配置10dBm，与固定发布／README一致 |

复用数字音频封装的格式／enable错误记录、显式传输超时、PCM16接口及软件音量／静音。PDM收到的已经是PCM16，直接读入既有录音管道；不调用标准I2S麦克风的32位槽缩放。输出沿用PCM16→32位槽转换，网络播放保持PCM24，软件AEC／唤醒继续现有路径。数字麦不调用模拟增益API。

本机IDF5.5.6的PDM默认DSR8S，16kHz对应名义PDM时钟1.024MHz；该采样率保留实际板类选项。厂家资料没有确定具体麦克风型号及最低工作时钟，本地不由构建结果宣称电气时钟符合器件要求。实际时钟、LEFT采样、录放音及声学AEC仍待实测。

GPIO45是扬声器时钟、48和41也不能照抄Doit电源或LED引脚。本板只初始化LED3输出，GAIN41及相机GPIO保持未使用状态。

## 无屏呈现与按键

复用现有手机热点配网页读取六位绑定码，配对在音频初始化之前完成；原有人工决策标记未改。页面与状态／提醒确认继续沿用既有热点接口校验、会话令牌及600秒查看窗，没有公网操作入口或永久开放窗口。

绑定后BOOT0首次释放才允许计时，持续按3秒重新打开查看窗，一次长按只触发一次，再次触发需释放。该键不同时触发录音或卡片确认，避免同一长按被两个动作竞争使用；语音采用唤醒词。原厂BOOT短按切换聊天尚未移植，操作卡在手机窗口处理。查看键任务在Wi-Fi启动后、联网等待／配对／音频初始化之前创建，只读取窗口手势；音频或联网失败后仍可打开页面。创建失败记录错误并停止本次启动，避免静默宣称按键可用。没有五击擦NVS等附带手势。

LED使用既有无屏状态逻辑：错误／提醒快速闪烁、配对／联网等待慢闪、说话／聆听亮起、空闲关闭，但引脚与极性按本板单独选择；Doit仍保持GPIO45低有效。

## 功率与未完成项

板级defaults设置 `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=10`、`CONFIG_ESP_PHY_MAX_TX_POWER=10`，本机SDK Kconfig明确前者单位dBm、后者默认派生前者。实际生成配置需核对，不把配置值当射频实测、有效距离或功耗证据。

OV3660／OV2640摄像头、图像识别／翻转、GAIN41控制及原厂BOOT短按动作未实现。该入口为基础Wi-Fi语音，不宣称完整AI摄像头设备功能。实际PSRAM／Flash、PDM时钟及麦克风型号、扬声器／增益默认值、LED、BOOT、APSTA／会话到期／配对码／提醒确认、10dBm射频、AEC／唤醒和同板OTA均未实机验收。

## 构建与证据

新板首次完整缓存构建退出0，但初次stage早于最终PDM PCM16直读和前移查看键任务修正。同步最终代码、刷新目的时间后，在同一stage增量 `reconfigure build` 再次退出0；最终日志实际重编main和digital_audio。初次日志及初次digital_audio源快照保留，未把初次不完整分支镜像作最终交付。Doit／虫洞EYE初次与最终公共回归均退出0；Doit最终实际重编PDM文件，EYE不选择digital_audio，但最终main和完整源快照已同步。

新板实际完整构建命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board dfrobot-df-s3-ai-cam
```

最终三profile分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，只复用本机IDF5.5.6-dirty、匹配工具和17个既有组件副本，无安装。准确参数／环境和两阶段日志见 `cached_build_attempts_20261110_dfaicam_<profile>.json`。

| Profile | 应用字节 | 3MB应用分区剩余字节 | 结果 |
| --- | ---: | ---: | --- |
| dfrobot-df-s3-ai-cam | 1,579,872 | 1,565,856 | 完整与最终增量build退出0 |
| doit-s3-aibox | 1,581,296 | 1,564,432 | 旧PDM／无屏／反向LED回归退出0 |
| chd-esp32s3-eye | 2,244,624 | 901,104 | 旧有屏单codec回归退出0 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目身份／16MB容量／checksum／validation hash通过；`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认新板实际链接PDM RX初始化、digital_audio、dummy codec和headless portal；旧Doit仍为PDM无屏，EYE仍为ES8311有屏。实际编译清单没有为新板选择屏幕UI／ES8311板级初始化。

三份各119项输入摘要、112个main文件集合、合并defaults、实际sdkconfig（含两项PHY=10）、main／audio／wifi编译宏、二进制板型身份、应用和模型容量与工作区匹配。模型均2,203,819字节，在4MB模型分区内。初次／最终日志、初次PDM源快照、新产物／ELF、配置／依赖锁／编译命令／最终源快照／SHA-256位于忽略目录 `evidence/dfaicam-builds-20261110/<profile>/`。SDK已有改动、17组件override和旧LVGL弃用提示保留，不是干净发行构建。

实际清单同步 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前71官方变体有源码／117待源码、72个本地profile含1历史入口；累计44官方变体曾各自缓存编译通过。当前3 passed／41 stale／6历史依赖预检失败／138 not_run（其中21项已有源码），全部188实机not_run。

固定板类／NoAudioCodec／LED声明、厂家网页与本机SDK PDM／PHY定义的URL和SHA-256在 `board_research_v2.5.2.json`，原始文件归档于忽略目录 `evidence/dfaicam-source-20261110/`。没有下载安装或改SDK。

未运行测试套件／独立类型检查、安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。全量目标仍进行中。

收尾实际执行 `git diff --check` 退出0，本批未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。对照上批快照审查无屏／PDM／LED／按键路由diff；三份119输入与17／18项归档产物、9项固定／厂家／SDK源证据摘要匹配。配对人工决策标记及其函数实现保持原样，全部本批构建进程已结束。
