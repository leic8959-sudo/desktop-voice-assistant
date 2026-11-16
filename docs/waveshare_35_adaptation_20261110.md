# 微雪3.5与3.5B基础语音终端源码适配

后续更新：3.5B／3.49／3.49V2三个AXS目标已分别完成首次整板缓存构建，原版ST7796回归通过；原整帧DMA、触摸、电源差异保留，实机未验收。见[本批说明](axs_native_20261112.md)。

后续更新：微雪S3LCD3.5原版与LilyGO MVSRLoRa已移除各自外部ST7796依赖，保留原17条专用／12条默认表和复位120ms，并分别完成首次整板构建。3.5B的AXS QSPI分支未构建，见[本批记录](st7796_sdk_20261112.md)。

日期：2026-11-10，Asia/Shanghai。新增两个独立源码候选：`waveshare-esp32-s3-touch-lcd-3.5` → `assistant_ws35`、`waveshare-esp32-s3-touch-lcd-3.5b` → `assistant_ws35b`。两者均有实际显示、输入、音频和最小供电源码；真实依赖预检均在创建stage前停止，尚未目标编译，没有实物验收。

## 固定来源与修改范围

板级配置、命令表和原厂逻辑来自小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。另核对两款厂商原理图：ESP32-S3R8、W25Q128JVSIQ外置16MB Flash、8MB Octal PSRAM，I2C0为SDA8／SCL7；原理图PDF的可变URL、当前原文及SHA-256均归档，不由图纸确认用户实物修订。

精确阅读ST7796 1.4.0（esp-bsp `5a39b90736dad653869d101f069a06758082a39e`）和AXS15231B 2.1.0（esp-iot-solution `e8ae944284047eb1a3a89e2538dd35f487396fa6`）接口／实现，以及本机FT5x06 1.1.1、LVGL port2.9.0、TCA9554和IDF5.5.6 ST7789接口。合计25项来源记在research新增的两变体同组finding中，源码原文在忽略目录 `evidence/ws35-source-20261110/`。

新增 `waveshare_35_board.h/.cpp`、`waveshare_35_display.cpp`、`waveshare_35_commands.h`、两板manifest／defaults及 `third_party/xiaozhi_ws35/`。命令数据保留原MIT版权、许可和修改说明，未复制完整上游板类／UI类，不改项目整体LICENSE或规则文件。公共改动限CMake、main失败处理、board_profile／tft_board、audio_io的单codec选择及board_power的ALDO1分支。

## 显示差异与必要修正

两板逻辑画布均480×320、无坐标gap、GPIO6高有效背光，5kHz／10位PWM初始暗；UI初始化成功后才设置亮度。

| 板型 | 屏幕与接口 | 显示策略 |
| --- | --- | --- |
| 3.5原版 | ST7796，SPI3 mode0／40MHz，MOSI1／MISO2／CLK5／DC3／CS NC；BGR、invert、swapXY，无mirror | 两个480×20行内部DMA缓冲，LVGL port负责一次RGB565字节交换和异步完成回调 |
| 3.5B | AXS15231B，QSPI2 mode3／40MHz，CS12／CLK5／D0=1／D1=2／D2=3／D3=4；RGB，原生320×480，不作硬件轴交换 | 480×320整帧PSRAM缓冲与90度软件旋转，固定320×20行DMA缓冲连续输出 |

原版固定源定义17条ST7796厂商命令，却调用ST7789构造函数传vendor_config；本机IDF5.5.6 ST7789不读取vendor_config。这里选择真正的ST7796 1.4.0并传入全部17条活动命令，保留原顺序、payload、延时及COLMOD05，排除原来注释掉的MADCTL。不能把原版当成普通ST7789，或者将“代码有一张表”误当成已经发送该表。

B版保留全部34条AXS15231B厂商命令，包括初始物理320×480窗口。组件QSPI draw_bitmap每次设置CASET，row0使用RAMWR，后续行用RAMWRC，没有设置RASET；因此不能把任意局部脏矩形直接送入这条路径。本地用LVGL FULL模式保证每次完整480×320帧，按固定源自定义flush的90度映射旋转到320×480物理像素。

每帧从物理row0开始，24条320×20行数据顺序发送；复制到DMA条带时完成一次字节交换。PSRAM帧307,200字节，单DMA缓冲12,800字节，不按包分配增长的vector。组件忽略CASET返回值，本地用公开panel IO检查CASET，再用其相同QSPI framing发送RAMWR／RAMWRC；每条都等待真实完成，包括最后一条，之后才调用LVGL flush_ready。IO失败／等待超时后停止后续刷新并熄背光，保留可能仍被DMA占用的条带，绝不复用或释放它；LVGL源帧与DMA缓冲独立。

B版完成等待窗口为一秒。SPI参数发送／总线排队可能阻塞超过该窗口，不能声称严格的端到端一秒上限。AXS 2.1.0的reset／mirror／invert回调会忽略部分IO错误，初始化使用公开IO显式检查SWRESET、MADCTL00、inversion off和DISPON；厂商初始化仍复用组件API。方向、颜色、顺序窗口、吞吐、最后完成事件和错误后的生命周期都需要目标编译与实机验收。

## 触摸与按键

原版FT5x06地址0x38，保留官方初始化／presence路径，随后通过既有I2C panel IO直接读取0x02开始的7字节状态／首点记录；B版AXS触摸地址0x3b，按精确2.1.0协议发送11字节读取命令，读取8字节原始包。两者均为400kHz，单次I2C调用超时100ms，rst／int不再用未归属的ESP GPIO复位。AXS一次读取分两次I2C调用，不能把每次100ms说成总100ms。

直接读取的原因是：FT5x06通用坐标接口不暴露event，AXS2.1.0仅支持一个点，点数大于1时丢掉数据，可能被上层当成成功释放。本地明确检查原始count与event，只接受down／contact；up／无点正常释放，多指、保留event、越界或读取错误取消LVGL按压并等待释放。初始必须观察到释放，再响应新按压，不缓存失败包。

保留固定源mirrorX／mirrorY再swapXY的方向意图，但使用真实320×480触摸范围及零起点端点，输出 `x=479-raw_y`、`y=319-raw_x`。固定源在交换前使用横屏480／320上限，会产生超范围Y；本地修正边界并明确不认定方向已经物理标定。两板触摸原点、镜像与B版软件旋转的组合是实机验收重点。

GPIO0是独立BOOT键，采用既有先释放再说话／卡片输入；触摸卡片与说话入口仍存在。未复制原版“首次上电主动重启解决黑屏”逻辑，LCD由板级复位完成，启动失败尝试关PA并停止本次启动，不清NVS或panic重启。

## 扩展器、供电与音频

厂商原理图两板均为TCA9554地址0x20：P0 camera PWDN、P1 LCD_RST、P2 TP_INT、P3 SD_CS、P4 RTC_INT、P5 AXP_IRQ、P6 SYS_OUT、P7 PA_CTRL。PA是NS4150B，供电为VSYS且CTRL有上拉。

本地复用既有I2C寄存器位更新封装，仅操作P1与P7：先将输出锁存置低，再设这两位为输出，LCD低100ms后高100ms；PA初始关闭。逐次真实读取输出／方向寄存器核对掩码，PA另读input寄存器核对引脚电平，运行期100ms互斥。没有调用会整端口FF／FF重置的厂家TCA构造函数，也不驱动P0开启相机或改变SYS_OUT；其余位在更新时保留。断电上拉行为、已有端口状态与PA极性仍待实物检查。

AXP2101 ALDO1输出LVCC3V3供给ES8311和麦克风。复用公共最小封装：0x92写28并读回（3.3V），0x90在原值上OR1并读回，保留其他使能位；单次100ms超时，清理设备后等10ms。不复制禁用全部DC／LDO、DC1改压、相机BLDO1／BLDO2、充电电流／CV电压、睡眠／关机政策。B版尽管PMIC_ENABLE=0，原类仍无条件执行InitializeAxp2101；不能根据该宏省掉实际所需ALDO1供电。

两板单ES8311、mono24kHz、I2S0 BCLK13／WS15／DIN14／DOUT16，原版MCLK12、B版MCLK44，均use_mclk；共用已有单codec真实mute／MIC gain回调与软件AEC路径，不选择双ES7210/TDM分支。PA由TCA P7独立门控，只在实际DAC解除静音成功后开启，静音先关PA。增益沿用30dB，采样率、时钟、静音重开、功放上电声及AEC／唤醒均待实机。

未接相机／SD／RTC／IMU／电量／充电调参／自动休眠／stock MCP。未确认电池状态仍为未知，不由PMIC存在推断电量或充电状态。现有实物只有微雪1.8，屏控修订仍待确认。

## 实际验证与证据边界

两款新板分别实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile <profile> --destination I:\xy_board_builds_20261109\<profile>-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

均退出1且未创建目的目录：原版缺 `espressif/esp_lcd_st7796 1.4.0`，B版缺 `espressif/esp_lcd_axs15231b 2.1.0`。记录为 `native_dependency_preflight_20261110_ws35.json`，没有安装组件、编译或新板镜像；两目标矩阵build均not_run。

旧Kevin RGB和Kevin Box2分别同步源码／刷新变化文件时间，实际执行：

```powershell
idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build
python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin
xtensa-esp32s3-elf-nm.exe -C build/<app>.elf
```

两组构建／镜像检查／符号读取均退出0。日志实际包含main／audio_io／board_power和各自显示重编；Kevin Box2还重编waveshare_spi_audio。符号／选中源显示旧Kevin RGB仍选单ES8311与原生RGB，Box2仍为双codec／OLED／ALDO2。两者没有编译新35 helper／display或选中3.5的ALDO1／音频引脚，不能证明新板可编译或运行。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| kevin-yuying-313lcd | 2,243,248 | 902,480 |
| kevin-box-2 | 2,188,768 | 956,960 |

模型均2,203,819字节，在4MB模型分区内。各155个main文件集合与工作区一致；各162项输入摘要、项目身份、16MB配置、编译宏、checksum／validation hash均匹配；每组17项归档产物及源码快照在 `evidence/ws35-builds-20261110/`，记录为 `cached_build_attempts_20261110_ws35_<profile>.json`。使用既有IDF5.5.6-dirty、17个组件override与Python offline_guard，不是干净发行依赖构建。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：93官方源码变体／94本地入口含1历史入口／95待源码；累计52变体曾缓存编译通过。当前2 passed（Kevin RGB／Box2）、50 stale、6历史failed、130 not_run（其中35已有源码）；全部188实机not_run，全量目标继续。

未运行测试套件／独立类型检查、安装依赖、启动宿主长期服务、Git写操作、上传、发布、刷机或OTA。headless_ui、上批4B helper／display、双codec封装、网关和运行时摘要保留。

收尾实际执行 `git diff --check` 退出0；14项本批新增文件逐一 `git diff --no-index --check -- NUL <file>` 无空白错误，返回1代表新增差异。25项来源、两组各162项输入／17项构建归档产物摘要匹配；原版17条／B版34条命令数据与固定源逐项一致。原有92个本地入口及既有research finding保留，矩阵research／registry摘要和两目标not_run状态匹配。6项保护文件摘要相同，SDK HEAD仍为880a203a且保留既有NimBLE／idf_as_lib改动；两回归进程均已结束。
