# Kevin 3.13：376×960 RGB显示与16kHz语音入口

日期：2026-11-10，Asia/Shanghai。发布变体 `kevin-yuying-313lcd`，独立应用名 `assistant_kevin_rgb`。本批接入RGB专用显示生命周期和单ES8311音频；尚无实机验收，玻璃安装方向与刷新稳定性需验证。

## 固定来源和方向修正

来源为小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 中 `main/boards/kevin/yuying-313lcd/` 的config.h／config.json、pin_config.h、实际板类、GC9503驱动／header，以及同提交RgbLcdDisplay与Es8311AudioCodec。另读取了本机IDF5.5.6的RGB驱动／header、LVGL port2.9.0和三线IO组件1.0.1~1接口，按实际使用版本编写，不照搬上游IDF6专有字段。

源声明宽376、高960，RGB扫描时序也为376×960，却同时给RgbLcdDisplay传swapXY／mirrorX。已安装IDF的RGB驱动仅在外部绘图缓冲复制到framebuffer时转换像素；当LVGL直接使用RGB驱动内部帧缓冲时，驱动只切换帧、不旋转或镜像帧内像素，但swap仍会改变窗口裁剪范围。照搬该组合不能证明得到预期90度图像。

本地采用与发布尺寸、扫描时序和直接帧缓冲一致的**376×960原生竖屏**，不设置硬件swap／mirror，不宣称复现上游旋转效果。使用独立高屏布局和16px中文字体，内容／按键边界位于376×960内。实际玻璃方向、排线和上下左右仍须验机；动态横竖屏切换尚未实现。

## 显示接线与IO生命周期

| 部分 | 本地接入 |
| --- | --- |
| 控制器 | GC9503V，RGB565／16位并行，376×960 |
| 初始化三线IO | CS48／SCL17／SDA16，mode0、DC位、最大期望500kHz；仅初始化使用 |
| RGB时序脚 | HSYNC6／VSYNC5／DE15／PCLK7，显示使能NC |
| RGB数据D0–D15 | 47、21、14、13、12、11、10、9、46、3、20、19、8、18、17、16 |
| 背光 | GPIO4高有效PWM，启动先暗 |
| 像素时钟 | 16MHz；H脉宽8／后沿30／前沿30，V脉宽8／后沿16／前沿16，极性按固定源为0 |
| 帧缓冲 | RGB驱动分配两帧PSRAM缓冲，共1,443,840字节；两个10行DRAM bounce缓冲共15,040字节 |

初始化先发SWRESET、等120ms，再发源驱动RGB默认B1=0x10／COLMOD=0x50和36条**有效**vendor命令；全部数据、顺序、长度和延时保留，包括52字节gamma与SLPOUT／DISPON各120ms。注释中的另一套旧表未导入。数据头 `main/kevin_rgb_commands.h` 静态限制负载长度；源声明的Espressif2023／Apache-2.0版权、条款和改编说明位于嵌套 `main/third_party/xiaozhi_kevin_rgb/`，未修改项目整体许可。

三线IO只存在于本地初始化函数中。每个写入结果均检查，命令失败保留原错误，随后仍尝试删除IO并记录清理失败；只有初始化和删除都成功，才创建RGB驱动接管17／16脚。CS48保持不选中，没有把已删除的IO指针交给LVGL、回调或面板持久状态，也没有复制上游自动删除后还传IO指针的生命周期。

RGB初始化仅使用原生 `esp_lcd_new_rgb_panel` 的5.5.6字段：bits_per_pixel=16、num_fbs=2、bounce_buffer_size_px=3760、dma_burst_size=64和PSRAM优先帧缓冲。随后使用 `lvgl_port_add_disp_rgb`，开启内置双缓冲直接绘制、bounce模式和帧完成同步；io_handle为空，字节不交换。没有将RGB面板当成普通SPI IO注册传输回调，也没有在现有刷新接口之外建立第二套VSYNC任务。

该时序的名义帧率为 `16,000,000 / (376+8+30+30) / (960+8+16+16) ≈ 36.036Hz`，不是宏名所暗示的60Hz。实际像素时钟／帧率、DMA下溢、撕裂、双缓冲切换和CPU／PSRAM压力必须测量。现有LVGL port等待帧完成使用其内部信号量，故异常硬件下的超时／恢复也未验收，不以编译成功推断刷新健康。

## USB与供电／音频

RGB数据包含GPIO19／20，板级defaults关闭主、次USB Serial/JTAG控制台，选择UART0，SDK默认TX43／RX44。已核对S3 GPIO功能切换会关闭19／20的USB pad；这是源码依据，不能证明实际PCB、插线、ROM下载阶段或外部UART接线正确。本轮没有操作串口或设备。

音频为单ES8311，I2C0 SDA1／SCL2，I2S0 MCLK42／BCLK41／WS39／DIN40／DOUT38，输入输出16kHz mono，使用MCLK。PA45高有效，先锁存低再配置输出；静音／关闭先关PA，实际DAC取消静音回调成功才使能。复用既有检查实际MIC增益／DAC mute结果的单codec路径，codec的PA脚为NC由外部显式门控。

协议播放保持PCM24，物理16kHz输出使用已有24→16kHz FIR，与软件AEC使用同一滤波结果及既有静音／abort处理，没有另建重采样实现。BOOT0首次释放后接既有按住说话／操作卡；无触摸，不注册屏幕pointer。原厂启动期点击重配网等完整手势、相机、电池／充电和动态旋转未实现。

Flash16MB／Octal继承固定release默认，不是实物模组容量证明。真实Flash／PSRAM、三线协议／电源gamma、376×960方向／RGB字节序／数据脚、16MHz时钟／36Hz名义时序、帧同步与内存余量、背光／PA45极性、16kHz音频时钟／槽位／增益／AEC／唤醒、BOOT、联网／绑定／提醒和同板OTA仍需实机验收。现有实物只有微雪1.8，屏控修订未确认。

固定源与本机API共17项证据的URL／SHA-256记在 `board_research_v2.5.2.json` 的原有Kevin finding；原文位于忽略目录 `evidence/kevin-rgb-source-20261110/`。36条有效表对照、缓冲字节及名义时序计算另有记录，没有把来源调查写成设备运行结果。

## 实际构建与结果

新板实际完整缓存构建命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board kevin-yuying-313lcd
```

退出0，stage `I:\xy_board_builds_20261109\xy_cached_9315927aebb9`。使用本机已有组件，新增依赖声明 `esp_lcd_panel_io_additions 1.0.1~1` 与本地版本一致，没有安装。完整编译实际选择kevin_rgb_display，未选择通用waveshare_spi_display；没有新增非弃用warning，原有LVGL弃用提示保留。

既有 `chd-esp32s3-eye` 的16kHz单codec和 `lceda-course-examples-eda-tv-pro` 的24kHz数字音频分别同步最终源码／刷新变化文件时间，实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0；日志有main／audio_io／waveshare_spi_display实际重编。新板与旧板结果单独记录，没有拿旧板回归替代RGB目标构建。

| Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| kevin-yuying-313lcd | 2,243,248 | 902,480 |
| chd-esp32s3-eye | 2,244,624 | 901,104 |
| lceda-course-examples-eda-tv-pro | 2,206,144 | 939,584 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0；项目名assistant_kevin_rgb／assistant_chdeye／assistant_eda_tv、16MB、checksum／validation hash匹配。新板实际链接esp_lcd_new_rgb_panel、lvgl_port_add_disp_rgb、esp_lcd_new_panel_io_3wire_spi、RGB framebuffer接口和es8311_codec_new；旧板仍选择各自模拟／数字音频与SPI显示路径。链接成功不证明GPIO电气、PSRAM运行分配或屏幕已输出有效帧。

三份各147项输入摘要／140个main文件集合、编译宏、合并defaults和生成配置与工作区匹配；Kevin还确认UART_DEFAULT／SECONDARY_NONE、主次USB控制台关闭。模型均2,203,819字节，在4MB模型分区内。新源表36条有效命令的代码／负载／长度／延时与固定源逐项相同。

证据为 `cached_build_attempts_20261110_kevin_rgb_<profile>.json`；日志／产物／ELF、配置／编译命令／依赖锁、源快照与SHA-256在忽略目录 `evidence/kevin-rgb-builds-20261110/`。本机IDF5.5.6-dirty与17个本地组件override保持原样，Python offline_guard拒绝外网，不是干净发行依赖构建。核对过的四份SDK RGB／GPIO／UART源在当前SDK HEAD没有改动，但SDK其他既有改动保留。

实际 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：88官方源码变体／89本地profile含1历史入口／100待源码；累计52变体曾各自缓存编译通过。当前3 passed／49 stale／6历史依赖预检失败／130 not_run，其中30项not_run已有源码；全部188实机not_run。全量目标仍进行中。

本轮未运行测试套件／独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。人工决策配对函数、网关／运行时和自然插话文件未修改，保留编辑前已存在的工作区改动。

收尾实际 `git diff --check` 退出0，本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。三份各147输入／16项构建归档产物、17项固定源／本机API摘要匹配；36条有效vendor表及RGB16数据脚／时序脚与固定源一致，帧大小可被bounce大小整除。矩阵研究／registry摘要与新板passed／实机not_run状态匹配，headless_ui摘要与上批一致；对照编辑前快照审阅本批diff，三个构建进程均已结束。
