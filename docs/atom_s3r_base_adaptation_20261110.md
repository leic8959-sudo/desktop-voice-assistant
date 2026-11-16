# 带屏 AtomS3R＋Echo Base 源码候选（2026-11-10）

后续更新：本页保留此前候选范围；AtomS3R EchoBase/Pyramid与Magiclick2.5现已移除各自外部GC9A01依赖，保留原GC9107专用表和识别，两款AtomS3R首次整板构建通过，Magiclick整板仍受GCC内部错误阻塞。最新来源与实机边界见[本批记录](gc9107_sdk_20261112.md)。无PSRAM AtomS3仍独立受阻。

新增发布变体 `m5stack-atoms3r-echo-base` 的基础Wi-Fi语音源码入口，独立应用 `assistant_atoms3rbase`。固定参照小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`。**本机缺GC9A01，目标板尚未编译或实机验收。** 无屏CAM/M12与本板不同，两个入口分别登记；全量仍154目录／188变体。

## 总线、按键与音频

- README明确8MB Flash／Octal PSRAM；板级defaults显式8MB／Octal，使用既有8MB分区：两个0x2A0000应用分区及0x240000模型分区。
- 音频底座在I2C1 SDA38／SCL39；ES8311、24kHz mono16、BCLK8／WS6／DIN7／DOUT5、无MCLK，BCLK派生codec时钟。这里不能套CAM/M12的I2C0，BSP_I2C_NUM与实际总线均设1。
- 复用上一批Atomic Echo Base的PI4IOE43 P0专用功放控制、厂家ID校验／读改写／回读、3.3V硬件增益和实际增益／静音回调检查；待机30dB、播放12dB，软件AEC和唤醒，不声明硬件参考或阵列处理。底座v1.0依据见[上一批厂家原理图记录](atomic_m12_base_adaptation_20261110.md)。本批未改该适配器内部实现。
- 本板实际存在BOOT41，首次释放后接受按住说话／无触摸操作卡；不沿用CAM/M12的无键重启手机查看入口。屏幕显示绑定码与卡片，没有虚构触摸。
- 正常使用需按厂家README从Echo Base底座USB-C供电。IMU、IR、RGB、其他传感器和底座热插拔恢复未接入。

## 屏幕识别与两种修订

128×128，SPI3 mode0／40MHz，MOSI21／CLK15／DC42／CS14／RST48，无GPIO背光。色序BGR、RGB565。复用既有128方屏布局和16px中文字体。

新增 `atom_s3r_detect_panel()` 在创建正式LCD IO前完成临时三线SPI读：先设CS高、RST低、DC命令态，低10ms／高120ms；临时SPI设备16MHz、half-duplex／3-wire、8位命令、1位dummy。保持CS未选中发一次0命令／0数据时钟，然后手动CS低读RDDID0x04的32位返回值，按原板类的低字节在前组合。所有GPIO／SPI／移除设备错误检查，读完解除片选并移除临时设备。

| 识别条件 | 显示驱动与初始化 | 最终镜像／反色 | gap |
| --- | --- | --- | --- |
| ID低16位0x7683或0x897C | ST7735，按实际板类使用ST7789兼容驱动 | mirrorX＋mirrorY／反色 | 2／3 |
| ID低24位0x079100 | GC9107，GC9A01 2.0.4驱动＋专用19命令 | 不镜像／不反色 | 0／32 |
| 其他ID或读取失败 | 停止显示初始化并记录错误 | 不猜屏控 | 不使用默认回退 |

最后一行有意不同于上游未知ID默认GC9107；不能把读取故障当作识别成功。两条驱动路径都将同一镜像传给LVGL，保持注册前后方向一致。固定SpiLcdDisplay构造传offset0／0，仅非零参数才设置LVGL offset，所以原来显式panel gap得以保留；本地同样只设panel gap，不叠加第二个偏移。

GC9107的19条专用命令、两条14字节gamma、FE／EF零长度数据均与固定原表数值对照一致。该表没有COLMOD覆盖，GC9A01驱动保留RGB565的0x55；它不同于Magiclick表的COLMOD05，不复用同名屏控的另一板表。固定组件实现先SLPOUT等待100ms再发自定义表；不另加猜测序列。命令数据MIT来源和许可保留在 `main/third_party/xiaozhi_atom_s3r/`，未改整体LICENSE。

## LP5562 背光

内部I2C0 SDA45／SCL0，LP5562地址0x30。原板类使用白光PWM寄存器0x0E，不是ESP LEDC GPIO，不能把NC传给PWM驱动。

新增板级背光初始化／亮度接口，以小智实际寄存器操作为依据，并只读交叉核对Linux v6.12 LP5562公开寄存器定义（没有复制GPL驱动函数）：

1. 先W PWM0x0E写0并回读，使背光关闭。
2. ENABLE0x00 mask0xC0写0x40，选择chip enable／linear PWM，等待1ms（公开驱动说明ENABLE需>488μs）。
3. CONFIG0x08 mask0x41写0x41，内部时钟和PWM_HF位。
4. LEDMAP0x70仅清W映射位0xC0，白光用直接PWM；保留RGB映射、各颜色电流和engine执行位。
5. 亮度0–100按整数 `percent*255/100` 写白光PWM，全字节回读；100ms超时和独立互斥锁，错误返回显示层。

clock／linear设置是芯片全局参数，仍会影响共用芯片的工作模式，不宣称所有RGB行为完全不变。白光电流配置按原板类保留，没有推测改大电流。实际白光接线、频率、电流、亮度和启动无闪烁需设备验收。

## 缺依赖与未完成

实际执行 `python -B tools/prepare_cached_firmware_build.py --profile m5stack-atoms3r-echo-base --destination I:\xy_board_builds_20261109\atoms3r-echo-base-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`，退出1：缺 `espressif/esp_lcd_gc9a01 2.0.4`，没有创建stage或启动编译。记录和日志摘要在 `native_dependency_preflight_20261110_atom_s3r_base.json`、忽略目录 `evidence/atom-s3r-base-preflight-20261110/`。保留两种实际屏幕分支及完整依赖，没有为了编译而删掉GC9107。

无AtomS3R实物：panel ID时序／字节顺序／dummy bit、两个屏幕实际修订／颜色／偏移／gamma、LP5562背光、两路I2C与P0功放、24kHz BCLK派生／采样／关闭重开／增益／AEC、BOOT、8MB Flash／PSRAM及同板OTA均未验收。没有将依赖预检或旧板构建当作该板编译通过。AtomS3原版无PSRAM、Echo Pyramid独立时钟／功放等继续分别排队。

## 公共回归与证据

CAM/M12无屏入口与AiPi单codec有屏入口的公共回归均完成，分别在既有隔离目录同步变化main文件／注册表／defaults，并刷新变化文件目的时间，执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，两项退出0。日志实际包含main与audio编译。环境为本机IDF5.5.6-dirty及17个既有组件副本；没有安装。

| Profile | 应用字节 | 应用分区剩余字节 | 结果 |
| --- | ---: | ---: | --- |
| m5stack-atoms3r-cam-m12-echo-base | 1,617,616 | 1,134,896（0x2A0000分区） | 无屏／Atomic Base公共回归退出0 |
| xorigin-aipi-lite | 2,246,784 | 898,944（3MB分区） | 有屏／单codec公共回归退出0，保留AiPi专用分区 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目名／8或16MB容量／checksum／validation hash通过；`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认对应ES8311与无屏／屏幕、Atomic Base相关符号。新板的LP5562／三线ID读取／两种屏幕宏均未由这两项构建编译，结果只代表原profile。

逐份115项输入摘要、108个main文件集合、合并defaults、实际sdkconfig、main／audio／wifi编译宏、二进制板型身份和分区容量均匹配。模型均2,203,819字节；CAM/M12模型分区2,359,296字节，AiPi模型分区4MB。原始日志／镜像／ELF／配置／依赖锁／编译命令／源快照／SHA-256归档于忽略目录 `evidence/atom-s3r-base-builds-20261110/<profile>/`，轻量记录为 `cached_build_attempts_20261110_atom_s3r_base_<profile>.json`。SDK已有改动、17组件override及既有LVGL弃用告警保留，不是干净发行构建。

实际同步命令 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前67官方变体有源码／121待源码、68个本地profile含1历史入口；累计41项官方变体曾各自缓存构建通过。当前2 passed／39 stale／6历史依赖预检失败／141 not_run（其中20项已有源码，含本板缺依赖），全部188实机not_run。固定原板类／显示类／音频类及公开GC9A01 API、Linux寄存器来源与SHA-256见 `board_research_v2.5.2.json`，原始文件归档于忽略目录 `evidence/atom-s3r-base-source-20261110/`。

未安装依赖、启动宿主长期服务、运行测试套件或独立类型检查、Git写入、上传、发布、刷机或OTA。只有现有微雪1.8实物，全量适配目标仍进行中。

收尾实际执行 `git diff --check` 退出0，本批新增／未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。对照上一批源快照检查板级／音频／屏幕／布局及构建路由diff；两份115输入与16项归档产物摘要、10项固定源／公开API／寄存器证据摘要匹配。所有本批构建进程已结束。
