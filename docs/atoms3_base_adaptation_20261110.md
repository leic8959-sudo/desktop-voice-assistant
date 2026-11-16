# AtomS3＋Echo Base源码候选

日期：2026-11-10，Asia/Shanghai。新增 `m5stack-atoms3-echo-base` → `assistant_atoms3base`，8MB Flash、无PSRAM、128×128文本屏和手动语音。已实现具体显示、底座、音频、按键及配置入口，真实依赖预检缺GC9A01 2.0.4而停止，目标尚未编译；不能宣称AtomS3可运行或已实机适配。

## 固定来源和复用

使用小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的AtomS3＋Echo Base config／config.json／板类／README。它明确8MB Flash、CONFIG_SPIRAM=n，没有本地语音唤醒／音频前处理；底座应从Echo Base USB-C供电。未把AtomS3R的PSRAM、LP5562背光和内部I2C0套入AtomS3。

新增 `atom_s3_display.cpp`，把旧 `atom_s3r_board.cpp` 中已有的识别函数原样移至 `atom_panel_id.cpp`；公共函数名 `atom_s3r_detect_panel` 保留，使旧S3R调用保持一致，新旧调用使用各自tft_board引脚。原S3R背光封装只增加显式I2C头文件，LP5562行为未变；两种S3R显示profile的CMake增加新的识别单元。

AtomS3的19条GC9107命令与已有 `atom_s3r_panel_commands.h`逐项一致，复用同一张MIT许可表，不复制第二份；新文件注明来源与复用边界。底座直接复用已有 `atomic_echo_base.cpp`，保留0x18／0x43 presence、PI4IOE ID、P0掩码更新／读回，不添加全地址扫描、整端口复位或热插拔重启。

10项来源及现有封装快照／摘要在忽略目录 `evidence/atoms3-base-source-20261110/`；固定四项公开URL记在已有AtomS3 research finding，未重复追加。GC9A01接口只参考先前缓存的2.0.4公开源，未安装组件或执行其脚本。项目整体LICENSE及规则文件未改。

## 显示与按键

SPI3 mode0／40MHz，MOSI21／CLK17／CS15／DC33／RST34，128×128 BGR RGB565，GPIO16高有效8位PWM，启动先锁存低保持暗。一个128×10行内部DMA缓冲2,560字节，LVGL port处理一次字节交换和异步完成；UI就绪后才设置亮度。没有创建S3R的LP5562／SDA45／SCL0路径。

共用识别函数保留原实现主体：16MHz半双工3线、RDDID04、一个dummy bit、32位读取，结束后始终取消片选并移除临时SPI设备。低24位079100选择GC9107，低16位7683或897c选择ST7735兼容驱动；未知ID返回不支持并停止启动，未移植上游“未知也回退GC9107”的猜测策略。

| 识别结果 | 驱动及初始化 | 显示方向和窗口 |
| --- | --- | --- |
| GC9107 | 官方GC9A01 2.0.4 API＋固定19命令 | 不invert、不mirror、不swap；gap(0,32) |
| ST7735 | 与固定板类一致的IDF ST7789兼容API | invert、双mirror、不swap；gap(2,3) |

两种路径均保留。显示注册向LVGL传入实际镜像配置，避免之后覆盖；识别与构造后仍各执行对应复位，沿用原生命周期。新AtomS3显示和抽出的共享单元尚未目标编译，色序、窗口、ID实际读值、SPI时序与重置行为全部待实物。

GPIO41是屏幕按压键，使用既有先释放后的根页面按住说话／卡片短按选择、长按松手确认。它不是GPIO0 BOOT；专用卡片提示使用“屏幕键”。无触摸指针，未把机械屏幕键声明为电容触摸。

## 128文本卡片修正

前批内部文本模式沿用了OLED布局，128高屏幕的action按钮在y32，正文也从y30开始，互相重叠。本批把internal模式与真实OLED布局分开：128高时正文y34、高68（到101），按钮y106、高20（到125），两个按钮宽62／间隔4，完整覆盖128宽且不越界。标题／状态／六位绑定码和卡片动作仍走既有逻辑，OLED原布局不变。

这是代码坐标核对和私有参考编译证据，没有设备画面验收；字体、滚动、字形、实际屏幕按压与卡片更新／取消仍需AtomS3实机检查。保留DND时提醒未读、绑定码、错误与卡片确认，不用点云占内部RAM，也不显示“本地唤醒”使用提示。

## 音频、底座和内存

I2C1 SDA38／SCL39；I2S0单ES8311、mono24kHz，BCLK8／WS6／DIN7／DOUT5，MCLK NC并选择BCLK派生codec时钟。复用已有单codec检查与Echo Base 3.3V功放／DAC硬件增益；外置PA由PI4IOE P0控制，只有实际DAC解除静音成功才开启，静音先关P0。未直接驱动不存在的ESP PA脚。

无PSRAM profile显式选择 `ASSISTANT_BOARD_INTERNAL_RAM=1`：不初始化本地WakeNet／AFE／AEC，采样经FIR转换16kHz，保留generation／连续性／read-error与溢出取消及原manual request／ready／PCM／end握手。发送33槽、播放8槽，不上传待机PCM；不支持本地语音唤醒、自然插话或媒体语音唤醒。上批71,028字节协议启动请求是局部预算，不能当成整个设备运行内存。

默认复用既有8MB分区布局：保留NVS0x9000／0x4000、两个0x2a0000 OTA槽、0x240000模型分区及0x800000最大终点。手动模式不加载模型，但当前包布局保留模型区域，不把它解释为本地唤醒可用。新目标尚无实际镜像或解码分区表，本批只核对板级CSV等于既有8MB布局。

仍需测实际启动／配网／TLS重连、手动录放音、提示音和OTA的internal_free／largest_block／栈水位。文本参考BSS含64KiB LVGL池，慢网／ready延迟可能早于640ms的理论上限填满发送队列并取消本轮；没有实测这些边界就不能宣布AtomS3能稳定运行。未接IMU／IR、未作完整底座热插拔、没有实物升级或账号操作。

## 实际验证与证据边界

新目标实际执行：

```powershell
python -B tools/prepare_cached_firmware_build.py --profile m5stack-atoms3-echo-base --destination I:\xy_board_builds_20261109\atoms3base-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

退出1：`Required component is not installed: espressif/esp_lcd_gc9a01; nothing was staged`，目的目录不存在；记录 `native_dependency_preflight_20261110_atoms3base.json`。没有编译、组件安装或新板镜像，矩阵build保持not_run。新AtomS3及S3R／Pyramid共享识别单元都需在补依赖后实际构建，不能从下面旧板回归推断。

旧1.43C默认PSRAM profile及前批EDA TV内部文本参考分别同步最终文件／刷新时间，实际执行：

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=waveshare-esp32-s3-touch-amoled-1.43c reconfigure build
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=lceda-course-examples-eda-tv-pro reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -S --size-sort -C build/<app>.elf
xtensa-esp32s3-elf-size.exe -A -d build/<app>.elf
```

EDA参考的internal宏和PSRAM关闭仅在隔离registry／sdkconfig，不写官方profile，仍只是基础代码回归。两组构建／镜像／符号／section读取最终均退出0，项目名／8或16MB／checksum／validation hash正确。初次日志实际重编main／audio／power／display和所选旧显示；随后补上S3R背光封装所需显式I2C头文件，最终同步未选中的该文件并再次构建，记录最终源码摘要，不声称第二次重编全部文件。

| 构建 | 应用字节 | DRAM data | DRAM BSS | 外部RAM数据／BSS |
| --- | ---: | ---: | ---: | ---: |
| 旧1.43C默认／8MB | 2,544,944 | 34,096 | 103,584 | 137,332 |
| 私有EDA内部文本参考／16MB | 1,692,784 | 20,484 | 129,312 | 0 |

它们未选中atom_s3_display或atom_panel_id，不验证新屏控、I2C1底座或实际128文本布局。各163个main文件集合／170项输入摘要、编译宏、生成配置及临时覆盖逐项核对。总记录 `atoms3_base_regressions_20261110.json`保存两组源／产物摘要；只有默认1.43C另写标准 `cached_build_attempts_20261110_atoms3base_waveshare-esp32-s3-touch-amoled-1.43c.json`，私有参考不计为官方通过。

使用既有IDF5.5.6-dirty／17组件override和Python offline_guard，没有安装依赖。未运行测试套件／独立类型检查、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。保护文件含网关、运行时、协议和无屏绑定UI，人工决策逻辑未改。

当前97官方源码／98本地入口含1历史／91待源码，累计53官方变体曾缓存构建通过；当前1 passed（默认1.43C）、52 stale、6历史failed、129 not_run（其中38有源码），全部188实机not_run。当前实物只有微雪1.8，屏控修订未确认，全部适配目标继续进行。

收尾实际执行 `git diff --check` 退出0；22项新增／变更文件逐项 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1表示新增差异。6项保护文件和原97个profile保持摘要／内容一致；10项来源摘要、两组各163个main文件／170项输入及产物摘要匹配。共享识别函数主体未改，19条GC9107命令与AtomS3固定源一致；原有其他research保留，矩阵摘要与新AtomS3 source_profile_present／build not_run、全部188实机not_run匹配。两个最终回归进程均已结束。
