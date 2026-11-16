# AtomS3R CAM/M12＋Atomic Echo Base 适配（2026-11-10）

新增官方发布变体 `m5stack-atoms3r-cam-m12-echo-base` 的基础Wi-Fi语音源码入口，独立应用 `assistant_atomm12base`。固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`；厂家库、原理图和芯片数据手册分别核对。只有现有微雪1.8实物，本板无实机验收。全量清单154目录／188变体。

## 实际配置

- AtomS3R CAM与M12共用发布入口，README明确N8R8、无屏、无应用按键。8MB Flash／Octal PSRAM，复用已有8MB分区：应用各0x2A0000、模型0x5C0000／0x240000。
- I2C0 SDA38／SCL39，ES8311七位地址0x18；I2S0 BCLK8／WS6／DIN7／DOUT5，无MCLK引脚。24kHz输入输出、mono16，ES8311使用BCLK派生时钟；默认待机增益30dB，播放期间沿用共用12dB，软件AEC／唤醒。
- 固定config写了AUDIO_INPUT_REFERENCE=true，但实际Es8311类始终input_reference=false；构造最后的false是use_mclk=false，不是硬件参考开关。本地不声明硬件回采。
- 固定config还有BOOT41，但实际板类不构造按钮、README明确无按键。本地listen设NC并明确不走触摸／按键通道，不把GPIO41当作可用物理键。
- 摄像头／GPIO18电源或指示灯、LED、相机识别、底座热插拔恢复未接入；音频依赖底座实际连接，不创建虚构屏幕或相机控制。

## 厂家原理图与PI4IOE修正

厂家产品页链接 `Sch_ECHO_Base_v1.0.pdf`，本轮渲染并视觉检查完整原理图。图中PI4IOE5V6408的P0经CTRL连接NS4150B，P1–P7未连接；功放与codec均由3.3V供电。厂家库固定提交 `m5stack/M5Atomic-EchoBase@0d4e074127903d5d2b2a466c7d766584c32f7da8` 写方向0x03=0x6F，小智板类写0x6E，P0的方向位不同。

芯片官方数据手册DS40583 Rev3-5第9页经文本提取及渲染视觉复核：0x01包含厂家ID101、修订、复位中断及软件复位位；0x03位=1是输出；0x05是输出锁存器；0x07位=0才驱动、=1是高阻。上游把0x07清零的注释称为“high-impedance”，与数据手册语义不一致。不能把整字节0xFF／0x00静音或0x6E方向直接照搬。

本地新增 `atomic_echo_base.cpp/.h`：

1. 仅探测实际codec0x18和扩展芯片0x43，不扫描128个地址；I2C操作超时100ms。
2. 读0x01，验证厂家ID位7..5=101；没有写软件复位或其他控制位。
3. 先将0x05的P0锁存低，再将0x03的P0置输出、0x07的P0清高阻，保持功放关闭。全部读改写并写后回读，保留P1–P7和上拉／中断配置。
4. 静音／关闭先关P0，取消静音在codec真实mute回调成功后才开P0；写入或回读失败向上传播，失败时尝试再次关功放。运行时由现有音频锁及板级互斥锁串行化。
5. ES8311实际增益／静音回调结果单独检查，规避codec_dev包装层丢弃驱动返回值；硬件增益中的PA电压按原理图选3.3V，仅本板分支改变。

这里的寄存器回读是锁存器证据，不能当作示波器或实际扬声器静音验收。原理图对应v1.0；不同硬件修订仍需核实。

## 手机入口与未完成项

复用现有无屏配网页读取六位绑定码，仍在音频初始化前呈现；原有人工决策标记未改。本板无应用按键，绑定后再次查看入口已向牛牛给出选项；尚无回复时，**暂按推荐方案：每次重启开放既有10分钟热点查看窗**，并在手机页面说明。该行为是本地暂定实现，不标成人工确认的决策。

窗口沿用600秒到期、热点本机接口校验、会话令牌和既有操作确认约束。没有延长为永久开放，也没有将查看入口改成公网接口。绑定后启动仅开查看窗口；未配置网络仍走原配网流程。没有增加自动重配／清除NVS手势。实际APSTA、到期关闭、手机接入／绑定、提醒内容呈现及确认仍待设备验收。

其他M5板继续独立排队：AtomS3原版无PSRAM，当前AFE／唤醒／粒子等依赖外部RAM，不能复用N8R8参数；AtomS3R屏幕有GC9107／ST7735检测与LP5562背光；Echo Pyramid有Si5351外部时钟、STM32及独立功放。该CAM/M12入口不代表这些板型完成适配。

## 构建与证据

本板首次完整缓存构建退出0；加入最终PI4IOE ID检查和3.3V硬件增益参数后，同步原隔离目录并刷新变化文件目的时间，最终增量构建退出0。原AtomEchoS3R与敏思K08的初次及最终公共回归均退出0；新板最终日志实际包含 `audio_io.cpp` 与 `atomic_echo_base.cpp` 重编，旧板最终日志包含 `audio_io.cpp` 重编，初次日志有main编译。三份最终产物均与当前源文件匹配。

新板实际完整构建命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board m5stack-atoms3r-cam-m12-echo-base
```

最终三profile分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，只复用本机IDF5.5.6-dirty及17个既有组件副本；没有安装。参数／日志／环境见 `cached_build_attempts_20261110_atomic_m12_<profile>.json`。

| Profile | 应用字节 | 最小应用分区字节 | 剩余字节 | 结果 |
| --- | ---: | ---: | ---: | --- |
| m5stack-atoms3r-cam-m12-echo-base | 1,617,616 | 2,752,512 | 1,134,896 | 完整及最终增量build退出0 |
| m5stack-atom-echos3r | 1,615,968 | 2,752,512 | 1,136,544 | 旧无屏单codec回归退出0 |
| minsi-k08-dual | 2,207,248 | 3,145,728 | 938,480 | 旧有屏数字音频回归退出0 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目身份／8或16MB容量／checksum／validation hash通过。`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认新板实际链接atomic_echo_base初始化／功放函数、ES8311和手机portal；编译清单确认使用headless_ui／headless_codec_board，没有误选digital_audio／屏幕UI。旧入口分别仍为ES8311无屏和数字音频有屏路径。

三份各110项输入摘要、103个main文件集合、合并defaults、实际sdkconfig、main／audio／wifi编译宏、二进制板型身份、分区和模型容量均匹配。模型2,203,819字节，8MB板模型分区2,359,296字节，余155,477；K08模型分区4MB。初次／最终日志、镜像／ELF、配置、依赖锁、编译命令、源快照及SHA-256在忽略目录 `evidence/atomic-m12-builds-20261110/<profile>/`。SDK已有改动、17组件override和旧LVGL弃用提示保留，不是干净发行依赖解析。

`python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380`实际退出0。当前66官方变体有源码／122待源码，67个本地profile含1历史入口；累计41官方变体曾各自缓存构建通过。当前3 passed／38 stale／6历史依赖预检失败／141 not_run（其中19项已有源码），全部188实机not_run。

固定板类／音频类、厂家库源文件、厂家网页和PDF，以及原理图PNG、数据手册第9页PNG／文本均归档于忽略目录 `evidence/atomic-m12-source-20261110/`，URL与SHA-256在 `board_research_v2.5.2.json`。原理图PDF渲染有若干字体替代提示，完整图中CTRL／P0连线及3.3V标注清晰；另以厂家库及手册交叉核对。没有安装任何依赖。

未实测PI4IOE ID／P0极性和其他位保留、功放电压／音量、24kHz BCLK派生时钟／mono采样／关闭重开、AFE／唤醒、8MB分区、配网绑定／查看窗／提醒确认或同板OTA。未运行测试套件、独立类型检查、宿主长期服务、Git写入、上传、发布、刷机或OTA。全量适配目标仍进行中。

收尾实际执行 `git diff --check` 退出0，本批新增／未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。逐文件对照上批快照检查音频／无屏UI路由／配网／板级改动，110输入与17项归档产物及11项固定／厂家源证据SHA-256匹配。全部本批构建进程已结束。
