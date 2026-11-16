# Otto Robot：两套接线的运行时语音入口

日期：2026-11-10，Asia/Shanghai。官方发布变体 `otto-robot`，独立应用名 `assistant_otto`。本批接入基础Wi-Fi语音与默认ST7789显示；摄像头、运动和机器人控制服务未接入，实机状态仍未验收。

## 固定来源与板型探测

来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `main/boards/otto-robot/` 配置、实际板类、power_manager和README，以及固定NoAudioCodec实现。一个官方发布变体包含两套硬件接线，不能只实现其中一种却宣称覆盖整个变体。

复用实际板类的板型判别条件：GPIO15／16配置输入弱下拉，等待5ms，连续8次、间隔1ms都为高才选CAM硬件；任一低选无摄像头硬件。GPIO配置失败停止启动。判别后释放两脚的下拉和输入路由，再配置显示／音频；无摄像头版的15／16随后是扬声器时钟，不能长期保留下拉。板型映射只在单线程启动时设置一次，音频初始化要求已完成探测；在显示和音频任务启动前分别确定引脚与采样率。

这里的CAM表示硬件接线，不表示检测到摄像头模组。没有输出相机XCLK、访问相机PID、创建摄像头I2C或初始化拍照；不因未接摄像头而错误选择另一套音频线路。实际外部上拉／板卡修订和判别稳定性仍须实测，编译无法证明自动探测正确。

| 部分 | CAM硬件 | 无摄像头硬件 |
| --- | --- | --- |
| LCD | MOSI45／CLK48／DC47／CS NC／RST1／背光38 | MOSI10／CLK9／DC46／CS12／RST11／背光3 |
| 麦克风 | BCLK42／WS40／DIN41 | BCLK5／WS4／DIN6 |
| 扬声器 | BCLK42／WS40／DOUT39 | BCLK15／WS16／DOUT7 |
| I2S | I2S0共享时钟全双工，16kHz输入输出 | I2S0输入16kHz，I2S1输出24kHz |

两种显示均ST7789 RGB／RGB565、240×240，SPI3 mode3／40MHz、反色、无轴交换／镜像、gap0／0，背光高有效；复用现有240×240布局。不会按无摄像头版引脚去驱动CAM版的屏幕。

## 音频复用与动态采样率

两套音频都遵循固定NoAudioCodec的LEFT／32位槽与PCM16软件接口。CAM分支在I2S0同一次创建TX／RX，两者配置相同16kHz、槽宽／WS与BCLK，引脚同时明确DIN／DOUT；无摄像头分支继续独立I2S0／I2S1。播放软件音量、真实PCM静音、显式传输超时和格式／enable错误检查仍复用既有digital_audio封装，没有假设PA使能脚或模拟增益。

本地原有digital_audio固定24kHz输出已改为读取所选profile的输出采样率；其他板仍为原先编译期采样率，只有Otto在启动探测后设置16／24kHz。audio_io仅为Otto在初始化读取一次运行时采样率；麦克风始终编译期16kHz。CAM播放使用已经存在的24→16kHz FIR结果，同时作为实际送往扬声器的数据和软件AEC参考，保留原滤波状态／静音／abort处理；无摄像头版继续送24kHz原播放数据。没有复制第二套重采样器或把协议播放采样率改成16kHz。

共享I2S的启停、时钟、LEFT槽和滤波参考路径需要硬件执行验证，单镜像包含两个分支不等于两种实物均已运行。BOOT0首次释放后接既有按住说话和操作卡交互，没有完整移植原厂点击切换聊天／启动期重配网手势。

## 执行器与未完成项

不编译OttoController、movements、oscillator、机器人WebSocket8080服务、相机或原厂表情类；不创建机器人动作MCP或给舵机输出PWM。没有读取电池／充电ADC，也没有启用无摄像头版在Wi-Fi下存在争议的ADC2电量路径。

无摄像头配置中LCD CS12与right_hand_pin12确实重叠。本地保留固定板类LCD接线，因此若实物真的把舵机接到此共享网，屏幕CS会驱动该网；“没有舵机控制代码”不能消除这个电气冲突，更不能作为机械安全证明。使用该板前必须确认实际修订／接线是否使用这枚右手舵机，并解决共享脚；本轮没有接设备或刷机。其他CAM舵机43／44／5／6／4／7和无摄像头舵机39／38／17／18／8／12也未由运动控制初始化，电气默认状态与机械姿态仍未知。

16MB／Octal继承固定release默认，实际模组容量／PSRAM、探测上拉、两屏接线／色序／背光、16kHz全双工与24kHz单工时钟／槽位／启停、静音／AEC／唤醒、BOOT、联网／绑定／提醒及同板OTA均需验机。现有实物只有微雪1.8，屏控修订尚未确认。

## 同批K10核对：仍是待实现

`dfrobot-df-k10` 的配置宏写ES8311／ES7210，但实际 `K10AudioCodec` 创建的是ES7243E输入，扬声器直接写I2S而没有创建ES8311输出。TDM4输入、32位重复采样输出、37.5dB增益、MCLK配置及软件音量曲线需单独处理，不能套入现有双codec入口。

固定代码把名为es7210_addr的0x23传给codec I2C控制接口，而本机codec_dev1.5.11会将地址右移一位；该参数实际对应0x11，不是可以直接认定的7位0x23。输出配置的MCLK赋值被注释，零初始化又可能落到已用作BCLK的GPIO0；输入控制器／codec局部变量遮蔽成员，析构还处理未创建的输出设备。这些都不应原样照搬。

K10需要专用ES7243E＋数字扬声器实现、真实ADC地址／时钟核对及TCA9555按键／屏幕供电封装；本机还缺ILI9341组件。GPIO0是BCLK，不能按BOOT键配置。固定A／B长按音量方向在README与代码也不一致。已将这些事实和七项固定源码证据写入原矩阵研究清单，继续保留inventory_only，未新增K10源码入口、未运行K10构建或安装依赖。

Otto六项及K10七项固定源URL／SHA-256见 `board_research_v2.5.2.json`，原文归档于忽略目录 `evidence/otto-source-20261110/`；本地板型与I2S编排没有复制上游运动／相机应用或更改项目整体许可。

## 实际构建与结果

新板实际完整构建命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board otto-robot
```

退出0，stage为 `I:\xy_board_builds_20261109\xy_cached_ee57cf82adea`。全新构建实际编译otto_board、digital_audio、audio_io、main和显示入口，没有新增非弃用warning，原有LVGL弃用提示保留。实际编译／链接包含两套Otto运行时分支，仍不能证明实际GPIO采样、分支选择或两种硬件录放音通过。

共用音频路径分别用旧EYE16kHz模拟输出和旧TV Pro24kHz数字输出回归：同步最终文件并刷新变化文件时间后，分别实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=chd-esp32s3-eye reconfigure build` 和 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lceda-course-examples-eda-tv-pro reconfigure build`，均退出0。日志包含main／audio_io／waveshare_spi_display编译，TV还有digital_audio；旧EYE没有误选运行时板型helper。

| Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| otto-robot | 2,207,344 | 938,384 |
| chd-esp32s3-eye | 2,244,624 | 901,104 |
| lceda-course-examples-eda-tv-pro | 2,206,144 | 939,584 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0；独立项目名assistant_otto／assistant_chdeye／assistant_eda_tv、16MB、checksum／validation hash匹配。Otto实际链接otto_board_init／ready／camera_board和digital_audio；旧板分别保留ES8311与dummy数字音频。编译清单和链接符号均没有机器人控制／movement／oscillator／WebSocketControlServer／esp_camera_init，不能据此推断共享CS12网或机械姿态安全。

三份各139项输入摘要／132个main文件集合、编译宏、合并defaults、生成Flash／Octal／codec配置与当前工作区匹配。模型均2,203,819字节，在4MB模型分区内。固定配置与本地CAM／NON分支的引脚赋值已逐项对照，记录于忽略目录 `evidence/otto-source-20261110/pin-map-comparison.json`；该对照不是运行测试。

证据为 `cached_build_attempts_20261110_otto_<profile>.json`，日志／配置／编译命令／依赖锁、镜像／ELF、源快照和SHA-256在忽略目录 `evidence/otto-builds-20261110/`。使用既有IDF5.5.6-dirty与17个本地组件override，Python offline_guard拒绝外网，保留SDK原有修改；不是干净发行依赖构建。

实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：86官方源码变体／87本地profile含1历史入口／102待源码；累计51变体曾缓存编译通过。当前3 passed／48 stale／6历史依赖预检失败／131 not_run，其中29项not_run已有源码；全部188实机not_run。K10继续inventory_only，全量目标仍进行中。

未运行测试套件或独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。无屏手机配对人工决策函数、并发网关／运行时和自然插话文件未修改。

收尾实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。三份各139输入／16项构建归档产物、Otto6项／K10七项固定源摘要匹配；矩阵研究／registry摘要及Otto构建passed／实机not_run、K10inventory_only状态匹配。headless_ui摘要与上批记录一致，对照编辑前快照审阅本批diff，三个构建进程均已结束。
