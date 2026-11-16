# 星智金属1.54离散触摸键与基础语音接入

2026-11-09，Asia/Shanghai。继续全部154个发布目录／188个小智v2.5.2变体的目标。本轮增加金属1.54实际源码入口，当前28个本地profile对应27个官方变体（另含1个历史兼容名），161个官方变体仍待源码；源码、缓存编译和实机验收独立记录。

## 确切型号与基础功能

profile为`nologo-xingzhi-metal-1.54-wifi`，应用身份`assistant_xzmetal154`。按固定上游默认16MB Flash／Octal PSRAM构建，未测量购买实物容量。240×240 RGB ST7789，SPI3 mode3／80MHz，MOSI10／CLK9／DC8／CS14／RST18／背光13，反色、无镜像／gap；复用已有SPI显示及240px卡片布局。

单ES8311输入输出24kHz，I2C0 SDA41／SCL42，MCLK5／BCLK6／WS4／输入17／输出15／PA21。与ABS2.0相近，但供电输出是GPIO48，电源检测是GPIO47；本地初始化48高、47输入，不套ABS的47输出／48检测，更不自动执行另一修订的PWR脉冲。实际芯片路由、供电和关机仍需实物核对。

固定来源为小智提交`49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`的config、板类、CST816类、README和电源类：

- [金属版固定目录](https://github.com/78/xiaozhi-esp32/tree/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-metal-1.54-wifi)
- [固定虚拟键码读取](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-metal-1.54-wifi/cst816x.cc)
- [固定按键码／时间阈值](https://github.com/78/xiaozhi-esp32/blob/49b4380b3faa0ceadd28a3e78aaf0f311c46c57e/main/boards/nologo/xingzhi-metal-1.54-wifi/cst816x.h)

头中PCA9557标志在固定实际ES8311类中未被引用，该类使用GPIO PA；因此当前候选沿实际调用接GPIO21，而不是猜测一个扩展器端口。仍须电路／实机验证。没有复制上游触摸应用、音量回调、电池估算或关机波形。

## 三枚触摸键不是坐标触摸屏

I2C0的CST816设备地址0x15，0xA7读取芯片标识仅用于日志，0x02起读取6字节触点帧。有效单键坐标码为：

| 原始键码 | 本地操作 |
| --- | --- |
| `(40,600)`中键 | 根页按住说话；卡片短按选择，长按松手确认 |
| `(20,600)`侧键 | 本机对话音量增加；首次加10，持续2秒后每200ms加5 |
| `(60,600)`侧键 | 本机对话音量减小；首次减10，持续2秒后每200ms减5 |

它们不是240px屏幕内触点。`metal_keys.cpp`只解析这些确切码，不创建LVGL坐标指针，也不把config中的GPIO0当成外部可用操作键。`BSP_CAPS_TOUCH=0`表示本实现没有像素触摸输入，不表示整板没有触摸按键。启动或故障后必须读取一次无触点帧才可重新按下，开机期间一直按住不能自动开始录音。

新增薄的`ManualInputKey`边界区分None／Listen／VolumeUp／VolumeDown／Invalid；普通GPIO板仍由原引脚电平映射None／Listen，CoreS3等像素触摸板继续使用现有LVGL按住说话入口。金属版声明没有GPIO按键且不使用LVGL录音按钮，主循环不会对GPIO_NUM_NC做移位或配置，也不会触碰虚构的GPIO0操作。

本地音量调整复用原音频状态锁，限制0至100，修改对话音量；提醒音量、静音设置保持各自偏好。若当前扬声器播放提醒，按键只更新对话音量值，不把该值写到提醒通道。该设置只保存在当前固件进程，之后的服务器完整偏好更新可覆盖它；未增加反向同步协议或持久化声明。

## 故障和取消边界

I2C超时／错误、未知坐标或多触点帧返回Invalid，按错误状态变化记录日志。主循环将正在进行的卡片按压取消，清除active标记，不调用“正常松键确认”。如显示锁暂未取得，会保留取消待办并等待，完整松开且取消成功前不能重新操作。卡片ID／revision绑定及联网状态检查仍走原有操作函数。

原录音`listen_stop()`表示正常结束并提交音频，不能用于通信失败。新增的`control_protocol_listen_cancel()`只发布已有手动取消标记并释放held；标记先于held写入，以免采集循环先观察到普通松键。已有手动服务循环随后走取消路径，不作为新的完整录音结束提交。没有更改自然插话／完整唤醒词人工决策处的行为；control_protocol只在已有手动入口旁增加此函数。

从一枚仍按着的键切到另一枚键同样取消，主循环等待完全松开，不把中键滑到音量区当成卡片长按确认，也不把侧键滑到中键当作新录音。读取故障恢复后仍等待完整释放。这里只说明源码设计，真实I2C故障、采集线程竞态和边界手感尚无硬件验收证据。

## 未实现项目

金属版的新旧电源硬件有USB ADC检查、不同关机波形和深睡策略，不能复用ABS脉冲或简单GPIO低电平。本轮只建立正确初始供电／输入模式；软件关机、USB／电池遥测、休眠和充电感知未实现。音量只在本机暂存。当前基础入口存在不代表整板所有功能或设备交付已完成。

牛牛目前只有现有微雪1.8，金属版实物缺失。需取得对应修订后核对Flash／PSRAM、PA路由、CST816键码、首次松开、中键录音／卡片ID更替、I2C断连恢复／多键滑动、音量／静音／提醒隔离、24kHz收放音和回声／唤醒、配对／提醒、供电与同板OTA。

## 实际构建命令与结果边界

金属版独立执行：

```powershell
& tools/build_cached_firmware.ps1 `
  -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 `
  -ToolsPath C:\Espressif\official-5.5.6 `
  -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env `
  -Board nologo-xingzhi-metal-1.54-wifi
```

另对十一款已有独立目录执行`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`核对公共输入／音频修改，设置IDF_PY_BUILD_JOBS=2。全部使用已安装SDK5.5.6-dirty、17个既有组件override和Python外网守卫，没有下载安装。实际退出、镜像和源码摘要在下方补充；不能把计划命令或旧成功日志套成本轮完成。

没有运行测试套件或独立类型检查，没有刷机、OTA、运行服务启动、Git写入、上传或发布。网关／运行时的并发工作保留；此轮control_protocol的必要变化仅为新增手动取消入口。

## 最终实际构建／镜像结果

金属版完整缓存构建退出0；旧十一profile各自的`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<对应profile> reconfigure build`全部退出0。没有运行串口工具。十二份应用分别执行`python -B -m esptool --chip esp32s3 image_info --version 2 build/<应用名>.bin`，全部退出0，应用身份、8／16MB镜像设置正确，checksum及validation hash有效。

| profile | 应用字节 | 实际应用分区剩余 |
| --- | --- | --- |
| nologo-xingzhi-metal-1.54-wifi | 2,533,328 | 612,400（3MB分区约19%） |
| nologo-xingzhi-abs-2.0 | 2,531,264 | 614,464 |
| lc-s3-wifi-1.54tft | 2,531,088 | 614,640 |
| nologo-xingzhi-cube-1.54tft-wifi | 2,493,120 | 652,608 |
| zhengchen-1.54tft-wifi | 2,493,120 | 652,608 |
| bread-compact-wifi-lcd | 2,492,208 | 653,520 |
| waveshare-lcd-0.85 | 2,538,400 | 214,112（单应用0x2A0000） |
| lilygo-t-circle-s3 | 2,507,424 | 638,304 |
| lilygo-t-circle-s3-v11 | 2,510,416 | 635,312 |
| waveshare-amoled-1.8-v2 | 2,614,832 | 530,896 |
| waveshare-lcd-1.54 | 2,546,128 | 599,600 |
| waveshare-lcd-1.83 | 2,615,568 | 530,160 |

每份记录核对56份固件源码／配置／许可摘要及完整main文件集合，均与工作区相同；0.85依据自己的8MB分区输入。模型也在各自实际模型分区内。金属版本次实际编译metal_keys.cpp及主循环／音频／卡片取消路径；旧板则编译各自原有GPIO或LVGL输入分支。编译通过不替代真实I2C错误、跨键滑动、卡片更替／网络异常与采集线程的设备验收。

`firmware/assistant_espidf/cached_build_attempts_20261109_metal_<profile>.json`保存确切命令、SDK、56份摘要及产物记录；原始日志、镜像、ELF、模型、分区、输入快照、许可及执行脚本保存在Git忽略的`evidence/metal-builds-20261109/`。没有复制到网关OTA路径、上传或操作设备。

SDK只读复查HEAD仍为`880a203ae603d08a94d06aa01e0d6761958e5e9e`，已有NimBLE／idf_as_lib修改保留，镜像自报5.5.6-dirty。已有LVGL flag API弃用告警保留；新增metal_keys／手动输入／供电／音频代码编译无对应新增告警。17个本地override含历史无关BSP，仍非干净发行锁。

当前27个官方变体有源码，其中12个有当前源码缓存构建证据、15个已有源码未编译；161个官方变体无源码；188项本轮实机验收全部未执行。金属版软件关机／ADC／深睡及本机音量持久化均未完成，不能由基础构建推定整板交付成功。全部本轮构建进程已退出，目标继续。没有安装依赖、测试套件、独立类型检查、运行服务、Git写入、上传、发布、刷机或OTA。

最终编辑审查实际执行`git diff --check`，目标包括本轮CMake、main、display_ui及头文件、audio_io及头文件、control_protocol及头文件，退出0；29份本轮相关未跟踪文件分别执行`git diff --no-index --check -- NUL <文件>`，无空白错误，仅Git的LF／CRLF提示。矩阵确认12个passed_cached_sources均有当前输入摘要匹配。网关、运行时和自然插话并发修改保留；control_protocol只在原手动入口旁新增一行cancel函数，未覆盖原有43／8整体diff中的并行工作。
