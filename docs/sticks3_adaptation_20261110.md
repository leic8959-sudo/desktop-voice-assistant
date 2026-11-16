# M5Stack StickS3 适配（2026-11-10）

新增官方变体 `m5stack-stick-s3`，独立应用 `assistant_sticks3`。固定小智v2.5.2提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的实际板类、配置与音频类，另查厂家产品页及M5PM1 1.0.7固定提交 `be9a5456c007c333e7ac963f33bfde1ffa5d82ee`。只有现有微雪1.8实物，该板无实机验收。公开清单仍154目录／188变体。

## 屏幕与资源

厂家页明确ST7789P3 135×240，与固定板类的ST7789构造及LCD引脚一致；固定README误写CO5300，本地不采用该名称推断AMOLED驱动。8MB Flash／8MB PSRAM，按固定S3发布默认Octal模式设置。使用既有8MB分区，两应用各0x2A0000，模型0x5C0000／0x240000。

显示SPI3 mode0／40MHz，MOSI39／CLK40／DC45／CS41／RST21，GPIO38高有效PWM背光；RGB565／RGB、反色、无交换无镜像。可见窗口52／40：上游panel先gap0／0再通过SpiLcdDisplay的LVGL offset52／40形成窗口，本地沿用既有panel gap方式只设置52／40，不叠加LVGL offset。实际面板边缘和颜色仍需实机核对。

新增135×240窄屏布局，16px中文字体、按住说话和无触摸操作卡。BOOT11低有效，首次释放后才接受。USER12是真实按键，本批仅设上拉输入、保留不驱动；其原厂预设音量动作尚未接入。上游该动作注释同时出现70／80%，实际调用60，不能据注释宣称已移植。

## PM1 最小电源接入

系统I2C0 SDA47／SCL48，PM1地址0x6E。厂家页及实际板类明确G2高使能LCD／音频，G3控制AW8737功放；G3不是ESP GPIO。

已先检查厂家M5PM1组件1.0.7：支持现有master bus句柄和带错误返回的GPIO API，但其IDF master读写使用timeout=-1，且依赖本机没有的 `espressif/i2c_bus`。按照现有项目约定不安装或改已安装组件，本批只通过项目既有 `update_i2c_register_bits` 封装接入必要的四组寄存器，没有再实现完整PMIC库或复制厂家函数。

新增 `stick_s3_power.cpp/.h`：

1. 100kHz设备，I2C每次100ms。读0x00–03设备／型号／硬件／软件字节；第一次失败后等待800ms再读一次，借鉴厂家有限唤醒重试。厂家begin本身只记录这些字节，未提供固定ID常量，因此本地记录字节而不伪造已验证型号。
2. PM1 GPIO_OUT0x11先清G3；GPIO_FUNC0x16仅清G2／G3的两位功能域，选普通GPIO。
3. GPIO_DRV0x13仅清G2／G3位，推挽；GPIO_MODE0x10仅设置G2／G3输出。
4. OUT0x11只设置G2高、G3低，等待100ms，LCD／codec上电而功放保持关闭。每项读改写并回读，保留其他GPIO和寄存器位。
5. 静音／音频关闭先关G3；取消静音只在实际codec静音回调成功后开G3。独立互斥锁、超时与回读错误向上传播；初始化不完整仍允许错误路径尝试关PA，启用要求ready。

没有写充电、升压、其他电源轨、G0／G1／G4、按键双击关机或睡眠配置。上游启用充电／关升压／设置双击关机，本地不从语音适配推导授权改变这些已有电源策略。main若显示／电源初始化失败会记录错误及尝试关PA的结果并返回，不进入该处panic重启环。没有独立电源错误页或自动恢复。

回读只证明寄存器值，不能证明G2实际电压、AW8737模式／脉冲控制、静音／爆音或实际电量。本板PM1固件／硬件修订和错误处理需实机验收。

## 录放音与音量

ES8311地址0x18，I2S0 MCLK18／BCLK17／WS15／DIN16／DOUT14，24kHz mono16。尽管MCLK脚接线存在，实际上游构造明确 `use_mclk=false`，本地使用BCLK派生codec时钟，保留实际I2S接线。codec PA设NC，由PM1 G3独立控制。待机增益30dB、播放12dB、现有软件AEC／唤醒，实际增益和mute回调返回值另行检查。

固定板类把每次逻辑音量映射为 `round(volume*0.6)`。本地将三处硬件音量写入统一经过 `set_output_volume`：首次打开、偏好／提醒与对话通道切换、现有本地音量调整。仅StickS3宏内转换为整数 `(volume*60+50)/100`；保存与显示的逻辑音量不缩小，其他板原值直通。包括提醒通道在内不会绕过该硬件比例；实际音量和声学仍待测量。

USER12预设动作、充电／电量显示、IMU／IR、睡眠与原厂部分按键手势未完成。基础Wi-Fi源码和构建不能等同全部设备外设已适配。

## 构建与证据

新板完整缓存构建退出0；CAM/M12无屏单codec与K08有屏数字音频的公共回归分别退出0。只复用本机IDF5.5.6-dirty和17个既有组件副本，没有安装依赖。

实际新板命令：

```powershell
$env:TEMP='I:\xy_board_builds_20261109'
$env:TMP='I:\xy_board_builds_20261109'
$env:IDF_PY_BUILD_JOBS='2'
& tools/build_cached_firmware.ps1 -SdkPath C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6 -ToolsPath C:\Espressif\official-5.5.6 -PythonEnvironment C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env -Board m5stack-stick-s3
```

旧板在既有隔离目录同步变化main／注册表／defaults并刷新目的时间，分别执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。三份日志实际包含main／audio编译，新板还编译了stick_s3_power及SPI display，目标新宏确实在该板自身profile编译。

| Profile | 应用字节 | 最小应用分区字节 | 剩余字节 | 结果 |
| --- | ---: | ---: | ---: | --- |
| m5stack-stick-s3 | 2,247,808 | 2,752,512 | 504,704 | 完整build退出0 |
| m5stack-atoms3r-cam-m12-echo-base | 1,617,616 | 2,752,512 | 1,134,896 | 旧无屏单codec回归退出0 |
| minsi-k08-dual | 2,207,232 | 3,145,728 | 938,496 | 旧有屏数字音频回归退出0 |

三镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin`，项目身份／8或16MB／checksum／validation hash通过。`xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`确认新板实际链接PM1供电／功放控制、ES8311、显示和16px中文字体，旧入口分别仍是无屏单codec与数字音频路径。三处音量调用在源码审查中均经过唯一硬件换算入口；编译不证明实际音量或声学比例。

逐份119项输入摘要、112个main文件集合、合并defaults、实际sdkconfig、main／audio／wifi编译宏、二进制内板型身份、应用和模型容量均匹配当前工作区。模型2,203,819字节，8MB板模型分区2,359,296字节、余155,477；K08模型分区4MB。原始日志／镜像／ELF／配置／依赖锁／编译命令／源快照／SHA-256位于忽略目录 `evidence/sticks3-builds-20261110/<profile>/`，轻量记录为 `cached_build_attempts_20261110_sticks3_<profile>.json`。SDK已有改动、17组件override和既有LVGL弃用提示保留，不是干净发行构建。

实际清单同步 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0。当前69官方变体有源码／119待源码、70本地profile含1历史入口；累计42官方变体曾各自缓存编译通过。当前3 passed／39 stale／6历史依赖预检失败／140 not_run（其中21项已有源码），全部188实机not_run。固定板类／codec、厂家网页、M5PM1固定版本API／实现／I2C兼容层的URL和SHA-256在 `board_research_v2.5.2.json`，原始文件归档于忽略目录 `evidence/sticks3-source-20261110/`。没有将厂家库下载安装到缓存或修改其timeout。

8MB／OctalPSRAM、ST7789P3默认表与135×240布局、PM1身份／G2／G3／原策略保留、按键、24kHz派生时钟／采样／增益／静音／关闭重开、60%硬件比例、唤醒／AEC、配网绑定和同板OTA均未实机验收。未运行测试套件或独立类型检查、安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。全量目标仍进行中。

收尾实际执行 `git diff --check` 退出0；本批新增／未跟踪文件逐个执行 `git diff --no-index --check -- NUL <file>`，无空白错误（返回1为新增差异，仅有LF／CRLF提示）。对照上批源快照检查音频／PM1／显示／布局／主入口与构建路由diff。三份119项输入和16项归档产物、13项固定／厂家源证据摘要匹配。全部本批构建进程已结束。
