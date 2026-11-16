# 立创课程三板：无屏、OLED和彩屏语音入口

日期：2026-11-10，Asia/Shanghai。新增 `lceda-course-examples-eda-super-bear`、`lceda-course-examples-eda-robot-pro`、`lceda-course-examples-eda-tv-pro` 的基础Wi-Fi语音入口；电机、机器人控制与完整课程功能不由本批源码宣称支持。

固定来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`，三板各自config.h／config.json、实际板类和README，以及NoAudioCodecSimplex源。按实际构造和回调判断能力，不按“机器熊／机器狗／TV”名称猜屏幕或按键。

## 三款实际配置

| 入口 | 显示／输入 | 数字音频 |
| --- | --- | --- |
| Super Bear／assistant_eda_bear | 实际板类NoDisplay；GPIO38为唯一实际应用按钮，本地长按3秒开手机查看窗，语音用唤醒词 | 麦克风BCLK39／WS40／DIN41；扬声器BCLK48／WS45／DOUT47 |
| Robot Pro／assistant_eda_robot | SSD1306 128×64，I2C0 SDA12／SCL13／0x3c；实际按住说话键GPIO14，BOOT0未绑定动作 | 麦克风BCLK16／WS17／DIN18；扬声器BCLK39／WS38／DOUT40 |
| TV Pro／assistant_eda_tv | ST7789 240×240 RGB，SPI3 mode2／40MHz，MOSI11／CLK12／DC47／CS NC／RST48，背光38高有效；实际按钮BOOT0 | 麦克风BCLK5／WS4／DIN6；扬声器BCLK15／WS16／DOUT7 |

三板均使用标准I2S0麦克风16kHz／LEFT／32位槽采集、独立I2S1扬声器24kHz／LEFT／32位槽输出，软件音量／实际PCM静音，不假设PA使能GPIO，不调用数字麦克风的模拟增益。复用已存在的重采样、软件AEC、既有VAD与本地唤醒模型流程。

Super Bear／Robot Pro发布配置选16m分区；本地用现有16MB／Octal及双应用／模型布局构建各自独立身份。其README说可按实际模组改为8MB，仅是配置示例，不能证明本轮16MB镜像可用于8MB实物。TV Pro也继承固定release默认16MB／Octal；真实模组容量与PSRAM仍待确认。

## 引脚与机器人边界

Super Bear的TOUCH_BUTTON_GPIO14与RIGHT_FOOT_PIN14冲突，但实际板类只创建GPIO38按钮，并不使用触摸键宏。本地不读取GPIO14，不初始化GPIO11／14／13／12／21／10的六个舵机，不编译控制器、movement或oscillator文件，也不注册机器人动作。手机呈现复用现有无屏配对／错误／提醒页面，不写任何状态LED引脚。

GPIO38先释放后计时，长按3秒一次开启600秒手机查看窗，释放后才允许再次触发。它只控制窗口，不同时控制录音；查看键任务在Wi-Fi启动后、等待联网／配对／音频初始化之前创建，音频失败时仍可查看页面。创建失败记录板型并停止启动。无屏六位码仍采用牛牛已选定的手机热点配网页方式，原人工决策函数未修改。原厂GPIO38点击切换聊天和启动期重配网手势没有完整移植。

Robot Pro的GPIO14是实际的触摸模块数字按键，按住／松开控制语音；本地也用于既有操作卡交互，首次释放才接受。这里不注册LVGL屏幕pointer。BOOT0仅被原厂构造、没有实际动作，本地也不读取。GPIO39／40是本板扬声器引脚，必须跳过通用OLED板的音量键初始化／读取，否则会破坏音频；OLED总线也按12／13设置，没有套用普通面包板41／42。GPIO47／21／9／10的四条腿舵机及其机器人MCP控制器不编译或初始化。

Robot Pro的SSD1306在面板和LVGL中均设置无镜像、无反色，128×64与14px中文布局；OLED对比度仍使用原有封装。TV Pro复用240×240布局、16px中文字体；板级实际SPI mode2保持不变，发布选项名 `LCD_ST7789_240X240_7PIN` 不能用来推断mode3。TV Pro宏中的TOUCH40、ASR21及LED2均没有被实际板类注册，本轮不赋予它们动作。

本批“未初始化舵机”只说明固件没有这些控制代码，不能证明电气默认状态、通电姿态或机械安全。运动／课程MCP、原厂未移植按键手势、LED等仍未完成，不把基础语音入口当作完整机器人功能。

## 待验收

真实Flash／PSRAM、三套16／24kHz时钟与LEFT槽、增益／静音／AEC／唤醒、按钮极性／释放取消、OLED方向／128×64卡片、TV mode2／无CS接线／色序／背光、无屏手机AP／绑定码／会话到期／提醒确认、联网和同板OTA均需实机验收。现有实物只有微雪1.8，屏控修订未确认，三款EDA板都没有设备证据。

公开固定源URL和SHA-256记在 `board_research_v2.5.2.json` 的三项finding；各板5项固定源文件归档于忽略目录 `evidence/eda-source-20261110/`。没有复制上游机器人控制器或修改项目整体许可。

## 实际构建与结果

三新板分别实际执行完整缓存构建，均退出0：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board <profile>
```

Board依次为三个 `lceda-course-examples-eda-*` 完整profile；stage分别为Super Bear `I:\xy_board_builds_20261109\xy_cached_4da3e9b4ce7c`、Robot Pro `...\xy_cached_fc8fe3df9558`、TV Pro `...\xy_cached_564140453fea`。三个全新stage都实际编译main／audio_io／digital_audio及各自显示入口，没有新增非弃用warning；既有LVGL弃用提示保留。

为核对共用无屏查看键任务，旧 `dfrobot-df-s3-ai-cam` 同步源码／刷新变化文件时间后实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=dfrobot-df-s3-ai-cam reconfigure build`，退出0；实际重编main／audio_io／digital_audio，生成的两项PHY发射功率仍为10，旧板PDM／LED3路径保留。

| Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| lceda-course-examples-eda-super-bear | 1,576,912 | 1,568,816 |
| lceda-course-examples-eda-robot-pro | 2,155,472 | 990,256 |
| lceda-course-examples-eda-tv-pro | 2,206,144 | 939,584 |
| dfrobot-df-s3-ai-cam | 1,579,888 | 1,565,840 |

四镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 及 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0；独立项目名／16MB／checksum／validation hash匹配。Super Bear选择headless_ui／portal，Robot选择display_ui／oled_display，TV选择display_ui／waveshare_spi_display；三者均链接digital_audio／dummy codec／标准I2S，旧DFRobot仍链接PDM RX。编译清单与ELF均无机器人初始化控制器、movement／oscillator实现，不用缺少符号推断设备机械状态。

每份137输入摘要／130个main文件集合、编译宏、生成Flash／Octal／dummy codec配置、合并defaults和工作区匹配。Robot还确认14px CJK／Montserrat10配置。模型均2,203,819字节，在4MB模型分区内。证据为 `cached_build_attempts_20261110_eda_<profile>.json`，日志／镜像／ELF、编译命令／配置／依赖锁、最终源快照和SHA-256归档在忽略目录 `evidence/eda-builds-20261110/`。

本机IDF5.5.6-dirty及17个既有组件override保持原样，Python offline_guard拒绝外网；不是干净依赖发行构建。实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0，当前85官方源码变体／86本地profile含1历史入口／103待源码；累计50个变体曾缓存编译通过。当前4 passed／46 stale／6历史依赖预检失败／132 not_run，其中29项not_run已有源码，全部188实机not_run。全量目标仍进行中。

未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。原无屏六位绑定码人工决策函数、网关／运行时和自然插话文件未修改。

收尾实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。四份各137输入／16项构建归档产物、三板各5项固定源摘要匹配；矩阵研究／registry摘要与三新板passed／实机not_run状态匹配，headless_ui整文件摘要与上批记录一致。对照编辑前快照审阅本批diff，三个新板构建及旧板回归进程均已结束。
