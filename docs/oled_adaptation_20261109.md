# SSD1306 OLED三变体与小屏字体适配

日期：2026-11-09，Asia/Shanghai。目标仍为固定小智v2.5.2提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的154个发布目录／188个变体。本批新增三个源码入口后，共36个本地profile对应35个官方变体，另有一个历史兼容名；153个官方变体尚无本地实现。全部188项实机验收仍未执行，牛牛只有现有微雪1.8，屏控修订未确认。

## 硬件与独立镜像

| profile | 应用身份 | 屏幕／特有输入与供电 |
| --- | --- | --- |
| bread-compact-wifi | assistant_oled32 | SSD1306 128×32，额外GPIO47离散输入 |
| bread-compact-wifi-128x64 | assistant_oled64 | SSD1306 128×64，额外GPIO47离散输入 |
| nologo-xingzhi-cube-0.96oled-wifi | assistant_xzoled | SSD1306 128×64，RTC21初始输出高；无GPIO47入口 |

三款均为I2C0 SDA41／SCL42、地址0x3C、400kHz、X／Y镜像、无反色、无独立屏幕复位引脚；BOOT0、音量上40／下39。GPIO47是按键输入，不是像素坐标触摸，`BSP_CAPS_TOUCH=0`不等于板上没有这枚输入。

固定来源为[面包板目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/bread-compact-wifi)、[星智OLED目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-cube-0.96oled-wifi)，以及固定`main/audio/codecs/no_audio_codec.cc/.h`。config.json明确面包板发布的两个SSD1306高度；SH1106选项没有注册成本批变体。Flash16MB／Octal PSRAM来自同一提交根默认配置，并非实物测量。公开源码副本在忽略目录`evidence/oled-public-20261109/`，详细URL与事实进入调查JSON。

音频均为独立I2S：麦克风16kHz，BCLK5／WS4／DATA6；扬声器24kHz，BCLK15／WS16／DATA7。32位左声道槽，输入移位12并饱和成PCM16，输出把PCM16放入32位槽高位。复用已有`digital_audio`采集、软件音量、关闭／重开和显式PCM清零静音，不虚构PA GPIO，也不调用模拟增益接口。三款板级默认配置显式启用`CONFIG_CODEC_DUMMY_SUPPORT=y`，供现有数字音频接口使用。本地沿已有引擎使用输入I2S0／输出I2S1，上游Simplex使用相反控制器编号，物理引脚一致；实际时钟、槽位／极性、音量、静音、唤醒和AEC仍需实物核对。

## OLED显示与小屏字体

`oled_display.cpp`复用已安装IDF的SSD1306驱动和esp_lvgl_port2.9.0的单色转换，使用全屏RGB565缓冲交给组件转换为SSD1306页格式，没有另写位图打包器。128×32／64的像素缓冲分别为8／16KB；LVGL注册保留X／Y镜像。亮度偏好映射到SSD1306对比度，0表示屏幕关闭，恢复时重新打开；实际亮度观感不等同线性PWM。

OLED依赖要求`IDF>=5.5.6,<5.6.0`，显式使用当前官方SDK提供的`transaction_timeout_ms=1000`，不沿未设置时的无限I2C等待。SDK相关esp_lcd文件无本地改动，没有通过修改SDK添加接口。屏幕断连／总线锁死后的错误显示和恢复仍无物理证据。

现有定制`ui_font`的行高实际为31px；较早记录中的“16px字体”描述不能代表该字体真实行高。本批将宽度≤240px彩屏改为已有LVGL16号中文字体（行高20px），让20／22px标签能够容纳文字；宽度较大的板继续原字体。OLED使用已有14号中文字体（行高17px）及10号ASCII状态字体，新三款的板级配置启用这些现有字体，没有安装新依赖。

- 128×32：上方12px状态／所选操作／倒计时行，下方从y14开始的17px中文滚动行，完整落在32px内。正文包含中文所选操作、短按换项／长按确认说明、标题、详情；路线仍包含距离／预计时间。显示不下的内容自动水平滚动，倒计时更新不重置正文滚动。
- 128×64：保留状态、17px标题、17px详情和17px按钮行，所选按钮黑白反显；无坐标触摸，按钮由硬件键操作。
- 首次配对单独显示六位码；32px显示ASCII配对状态和居中代码，64px另有中文控制台提示。代码不会沿原彩屏中心偏移被裁切。
- OLED跳过点云内存分配和动画，只保留250ms的共享卡片／提示状态定时器及必要文字刷新。状态和失败／待确认提示使用小型ASCII行，中文正文保留。该布局是源码候选，未宣称已看过实物显示。

卡片仍复用原队列、TTL、ID／revision绑定、联网检查、后台明确确认和显示回执，不另建OLED业务状态机。请求已提交、失败、未确认与离线状态不会因为缩小屏幕而改成成功。单色状态消息保持到相应卡片刷新，避免每秒倒计时覆盖失败提示。

BOOT及面包板GPIO47都映射为Listen，根页按住说话，卡片短按选择／长按松手确认。启动先读取全部松开，多键同时按下返回Invalid，走已有取消并等待释放路径。音量键沿已有本机对话音量处理，不改提醒／静音偏好；与原上游长按直接0／100的手感不同，仍是当前进程状态。

星智只保留RTC21初始高电平／释放旧保持；充电GPIO38、电池估算、自动关机／深睡、LED等尚未接入。面包板示例灯控GPIO18没有配置或控制。基础语音源码不是整板所有扩展功能已交付。

## 构建命令与证据范围

为避免C盘再次耗尽，新构建使用本任务专用`I:\xy_board_builds_20261109`，只在当前PowerShell进程设置TEMP／TMP，不改用户持久环境设置：

```powershell
$env:TMP = 'I:\xy_board_builds_20261109'
$env:TEMP = 'I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS = '2'
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board <对应profile>
```

三款分别独立stage／构建，不使用另一款的日志代替。现有SDK仍为5.5.6-dirty，17个已有组件本地override、Python外网守卫，不安装依赖。最终源码会在各自目录再次执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`；旧RGB／16kHz输出／32kHz输入入口也按对应宏独立构建。实际结果、镜像身份与容量在本批结束后补充，不能把中间进度当作通过。

没有运行测试套件／类型检查、刷机、OTA、启动服务、Git写入、上传或发布。网关／运行时及自然插话改动保留。实际OLED画面、所有小屏字体回归、I2C断连恢复、按键手感和声学行为仍待实机验收。


## 本批构建失败的保留

首轮三个完整构建均在新OLED初始化的C++花括号循环处失败，实际wrapper退出1、内部idf构建退出2。源码已改为普通数组循环，原始`cached-build.log`保留，未把该次记为通过。

首次更新源码后的构建，32px与星智64px已完成应用源码编译，但链接发现`dummy_codec_new`未定义：新板默认配置漏启用已有dummy codec。已补齐`CONFIG_CODEC_DUMMY_SUPPORT=y`并使隔离sdkconfig与默认配置一致；没有安装新组件。面包板64px该次在ldgen生成链接脚本时Python进程以-1073741819异常退出，不能据此判定是源码或内存不足。三个第二次日志均另存`oled-regression-second-attempt.log`，64px另保留`oled-regression-tool-failure.log`。补齐codec开关会改变公共sdkconfig头，SDK目标会重新编译；最终结果以之后的实际命令为准。


## 最终实际结果

三款OLED在补齐配置后的各自`reconfigure build`均退出0。GenJuTech1.54（16kHz输入／输出及240px彩屏）、T-Circle V1.1（32kHz输入／24kHz输出及160px圆屏）、微雪AMOLED1.8-v2（368px彩屏）各自的回归构建也全部退出0。没有把首轮或第二轮失败日志改成通过；三份OLED记录均含三次实际尝试。

六份应用分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份／16MB设置正确，checksum及validation hash有效。

| profile | 应用字节 | 3MB应用分区剩余 |
| --- | --- | --- |
| bread-compact-wifi | 2,155,984 | 989,744（约31%） |
| bread-compact-wifi-128x64 | 2,155,792 | 989,936（约31%） |
| nologo-xingzhi-cube-0.96oled-wifi | 2,156,832 | 988,896（约31%） |
| genjutech-s3-1.54tft | 2,244,672 | 901,056 |
| lilygo-t-circle-s3-v11 | 2,223,440 | 922,288 |
| waveshare-amoled-1.8-v2 | 2,614,960 | 530,768 |

每个profile的61个源码／配置摘要及完整main文件集合与当前工作区一致；新三款隔离override manifest的IDF约束也与本地5.5.6最小要求一致，最终sdkconfig确实启用dummy codec。记录为`cached_build_attempts_20261109_oled_<profile>.json`，日志、镜像、配置和源码快照在忽略目录`evidence/oled-builds-20261109/`。SDK仍有原NimBLE／idf_as_lib修改，自报5.5.6-dirty；既有LVGL弃用警告及17个override中未使用的历史BSP保留。它们不是干净发行构建或所有板型的编译证据。

矩阵当前35个官方变体有源码，6项当前源码缓存构建通过，11项历史通过因公共文件变化为stale，3项GC9A01预检失败，15项尚未构建。累计17个官方变体曾各自编译通过；153项尚无源码，188项实机验收仍为not_run。不能用六项构建支持其他未选宏分支的结论。

`native_dependency_inventory_20261109_oled.json`静态核对36个本地入口：18个可找到直接缓存依赖，18个受阻；其中包含1个历史兼容名。它不是传递依赖解析或构建通过数。执行`git diff --check`及对8个新源码／文档文件的`git diff --no-index --check -- NUL <文件>`审阅，无空白错误，存在Git的LF／CRLF提示；另执行`python -B tools/sync_board_matrix.py --source-directory <既有公开元数据目录>`更新清单。未运行测试套件、类型检查或设备操作。
