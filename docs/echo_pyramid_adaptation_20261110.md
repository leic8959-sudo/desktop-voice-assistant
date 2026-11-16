# AtomS3R＋Echo Pyramid 源码候选（2026-11-10）

后续更新：本页保留此前候选范围；AtomS3R EchoBase/Pyramid与Magiclick2.5现已移除各自外部GC9A01依赖，保留原GC9107专用表和识别，两款AtomS3R首次整板构建通过，Magiclick整板仍受GCC内部错误阻塞。最新来源与实机边界见[本批记录](gc9107_sdk_20261112.md)。无PSRAM AtomS3仍独立受阻。

新增官方发布变体 `m5stack-atoms3r-echo-pyramid`，独立应用 `assistant_echopyramid`。固定参照小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。本机缺GC9A01，**该目标尚未编译或实机验收**。只有现有微雪1.8实物；全量仍154目录／188变体。

## 显示与总线

8MB Flash／Octal PSRAM，复用既有8MB分区：应用各0x2A0000，模型0x5C0000／0x240000。BOOT41低有效，首次释放后接受共用按住说话／无触摸操作卡。

AtomS3R屏幕128×128 BGR，SPI3 mode0／40MHz，MOSI21／CLK15／DC42／CS14／RST48。内部I2C0 SDA45／SCL0上的LP5562控制背光；复用上一批独立的白光PWM、读改写回读和超时检查。增加明确的 `ASSISTANT_BOARD_ATOM_S3R_DISPLAY` 共用显示标记，Echo Base与Pyramid分别保留音频和电源宏，避免误选单codec／P0功放。

三线16MHz RDDID0x04／1位dummy／32位读取与Echo Base相同：低16位0x7683或0x897C为ST7735，低24位0x079100为GC9107；未知ID停止初始化。两板19条GC9107表逐项相同，复用既有MIT命令表和许可，不重复新驱动；方向与窗口则不同：

| 型号 | GC9107 | ST7735兼容路径 |
| --- | --- | --- |
| AtomS3R＋Echo Base | 无镜像、不反色、gap0／32 | 双轴镜像、反色、gap2／3 |
| AtomS3R＋Echo Pyramid | 双轴镜像、不反色、gap0／0 | 无镜像、反色、gap2／3 |

面板与LVGL使用同一镜像值，panel gap仅设置一次，不叠加LVGL offset。复用128方屏布局与16px中文字体。本批共享并没有将两板的方向或音频接线合并成一个配置。

## 底座发现与外部主时钟

音频／电源在I2C1 SDA38／SCL39，仅读五个必须存在的七位地址：ES8311 0x18、ES7210 0x40、Si5351 0x60、STM32 0x1A、AW87559 0x5B。最多20轮，仅NOT_FOUND重试，轮间250ms；每个探测100ms，其他总线错误立即向上返回。没有全地址扫描或无限断连循环。Pyramid的显示／电源初始化失败由main记录错误和尝试关PA的结果，然后返回，不走该处现有ESP_ERROR_CHECK导致的panic重启。设备尚无独立屏幕错误页，失败时需串口查看，并人工重启重试；没有自动恢复。厂家README要求从Pyramid底座USB-C供电。

新板级实现 `echo_pyramid_board.cpp/.h` 将外部Si5351配置为24kHz×256=6.144MHz，仅使用CLK1。固定源代码的晶振参数为27MHz、PLLA884.736MHz、MS1除144，但计算 `uint32_t rest * 1000000` 会溢出：20,736,000,000,000超过UINT32上限，截断后b=155，原本应768000。不能原样移植这个结果。

本地使用约分后的精确比例，无需大数浮点或32位乘法：

- 27MHz × (32＋96／125) = 884.736MHz；再除144 = 6.144MHz。
- PLLA参数P1=3682、P2=38、P3=125；寄存器26–33为 `00 7D 00 0E 62 00 00 26`。
- MS1参数P1=17920、P2=0、P3=1；寄存器50–57为 `00 01 00 46 00 00 00 00`。

按固定板类关闭所有输出并关闭CLK0／CLK2，设置10pF晶振负载／PLLA晶振输入、PLLA与MS1、CLK1整数模式／8mA。两组8字节参数写后回读；普通字段读改写并回读。只向自清除复位寄存器177写PLLA reset0x20，不拿自清除位作锁存比较，也不复位未用的PLLB。500ms期限轮询状态SYS_INIT／LOL_A／LOS，全部清除才启用CLK1（输出使能0xFD）。失败保持输出关闭并返回。

时钟寄存器含义以Linux v6.12 Si5351公开头文件交叉核对，仅使用寄存器事实，没有复制GPL驱动函数。时钟专用于该底座，其他输出不提供服务。**这些是精确参数计算与寄存器检查，不是实际6.144MHz、电平、相位或声学测量**；晶振27MHz、芯片变体及外部MCLK与ESP产生的BCLK／WS的实际相位和频率一致性仍待硬件验收。

## STM32、功放与音频

启动先核对AW87559寄存器0x00的ID=0x5A并关闭PA。时钟就绪后，仅向STM32写A0=1这个固定板类的扬声器复位命令，等100ms，再核功放ID。没有发送RGB亮度／状态页命令。

AW87559 SYSCTRL寄存器0x01的SW bit6／boost bit4启用，PA bit3仍关闭；增益0x06低5位设11（固定板类标注16.5dB），读改写和回读保留其余位。取消静音须在实际codec mute回调成功后才开PA；静音／关闭先关PA，失败传播并尝试关回。STM32复位写超时也可能已经执行，因此完整ready前的错误路径允许先重新核对AW ID再尝试关PA；启用PA仍要求完整初始化成功。运行时独立互斥锁，每次I2C100ms。每次真正打开音频再核Si5351锁定状态和AW ID；不循环重置或自动重连。

ES8311输出＋ES7210输入，I2S0 BCLK6／WS8／DIN5／DOUT7，没有ESP的MCLK输出脚，但codec实际使用Si5351外部MCLK；保留固定输出codec的5V／3.3V硬件增益参数，不套Echo Base的3.3V单codec配置。24kHz、16bit、标准I2S两槽，ES7210仅选MIC1＋MIC3，读取slot0／MIC1为单声道，待机30dB／播放12dB增益。共用I2S超时收发、实际格式／enable错误检查、增益／静音回调结果检查与软件AEC。

现有codec_dev1.5.11在选中至少3个MIC时才切TDM；MIC1＋MIC3只有两个，因此本板保持STD2，不套其他BOX的TDM4。固定上游另取slot1作硬件参考，本地未消费该参考，MIC3保持驱动默认增益，不声明双麦同时处理或硬件回采AEC。ES7210 MIC1／MIC3物理槽位和外部时钟正确性必须实机测量。

## 依赖与未完成

实际命令 `python -B tools/prepare_cached_firmware_build.py --profile m5stack-atoms3r-echo-pyramid --destination I:\xy_board_builds_20261109\echo-pyramid-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6` 退出1：缺 `espressif/esp_lcd_gc9a01 2.0.4`。没有创建stage或启动目标编译，也没有安装；保留两套实际面板路径。日志和SHA-256见 `native_dependency_preflight_20261110_echo_pyramid.json` 及忽略目录 `evidence/echo-pyramid-preflight-20261110/`。

STM32 RGB／灯效、底座热插拔／断连恢复、其他执行器未实现。所有新时钟／STM32／AW87559／STD2／面板宏均未编译；旧入口回归不能替代。实际Si5351晶振／频率／锁定状态、STM32复位影响、功放增益／极性／爆音、MIC映射／AEC／唤醒、面板ID／方向／背光、BOOT、8MB容量、联网绑定及同板OTA均未验收。

## 公共回归与证据

微雪Audio Board双codec与CAM/M12单codec无屏入口的初次和最终公共回归均退出0。实际在既有隔离目录同步main／注册表／defaults并刷新变化文件目的时间，分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`；初次日志实际包含main／audio编译，补齐Pyramid特有的启动失败处理后，第二份日志包含main实际重编。最后同步PA关闭边界的Pyramid专用helper修正，旧profile不选择该helper，因此最终构建没有应用对象重编，但完整main源集合与当前代码一致；三次命令均退出0，新宏仍未编译。环境仅为本机IDF5.5.6-dirty、匹配工具和17个既有组件副本，没有安装。

| Profile | 应用字节 | 应用分区剩余字节 | 结果 |
| --- | ---: | ---: | --- |
| m5stack-atoms3r-cam-m12-echo-base | 1,617,616 | 1,134,896（0x2A0000分区） | 现有无屏／单codec／P0控制回归退出0 |
| waveshare-audio-board | 2,256,096 | 889,632（3MB分区） | 现有JD9853／TDM4双codec回归退出0 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目名／8或16MB容量／checksum／validation hash通过；`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认现有ES8311／无屏portal／Atomic Base与微雪ES7210／JD9853等符号。编译清单明确不含 `echo_pyramid_board.cpp` 和 `atom_s3r_board.cpp`，没有将旧profile结果算成Pyramid目标通过。

逐份117项输入摘要、110个main文件集合、合并defaults、实际sdkconfig、main／audio／wifi编译宏、二进制板型身份、应用和模型容量均匹配当前工作区。模型2,203,819字节，在CAM/M12的2,359,296字节模型分区和微雪4MB分区内。原始初次／最终日志、镜像／ELF、配置、依赖锁、编译命令、源快照及SHA-256在忽略目录 `evidence/echo-pyramid-builds-20261110/<profile>/`，轻量记录为 `cached_build_attempts_20261110_echo_pyramid_<profile>.json`。SDK既有改动、17组件override和LVGL弃用告警保留，不是干净发行构建。

清单实际同步 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前68官方变体有源码／120待源码、69个本地profile含1历史入口；累计41项官方变体曾各自缓存构建通过。当前2 passed／39 stale／6历史依赖预检失败／141 not_run（其中21项已有源码，含Pyramid缺依赖），全部188实机not_run。固定板类／显示类、Linux寄存器头与已安装ES7210驱动的源码及SHA-256记录在 `board_research_v2.5.2.json`，归档于忽略目录 `evidence/echo-pyramid-source-20261110/`；该目录另含 `clock-arithmetic.json`，明确标注仅为整数参数计算。

未安装依赖、运行测试套件／独立类型检查、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。全量目标仍进行中。

收尾实际执行 `git diff --check` 退出0，本批新增／未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。两份117输入和18项归档产物、8项固定／寄存器／本机codec源证据摘要匹配。源数组解码确认为PLLA(3682,38,125)、MS1(17920,0,1)，名义CLK1=6,144,000Hz；仍不是实测频率。所有本批构建进程已结束。
