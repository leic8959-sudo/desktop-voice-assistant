# 微雪S3 CAM四屏：源码候选与CH32依赖

后续更新：CAM四种屏幕已分别完成首次整板缓存构建，采用核对过的CH32协议，3.5保留ST7796默认初始化；CHD3.5回归通过。整端口复位、外围未接入和实机限制仍在。见[本批记录](ws_cam_native_20261112.md)。

日期：2026-11-10，Asia/Shanghai。接入官方发布变体 `waveshare-esp32-s3-cam-1.83`、`-2`、`-2.8`、`-3.5` 的基础Wi-Fi语音源码；新板目标全部缺依赖，尚未编译或实机验收。

## 来源与独立屏幕配置

小智v2.5.2固定提交 `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` 的 `main/boards/waveshare/esp32-s3-cam/` 实际板类、config.h／config.json及README。厂商原理图固定于 `waveshareteam/ESP32-S3-CAM-OVxxxx` 提交 `72af9302030bb6f018d5c21b1225005481e09fda`，已提取整页文字并查看整页和引脚表／扩展器／功放三个局部渲染，未改原图。

| 变体 | 应用身份 | 显示 |
| --- | --- | --- |
| 1.83 | assistant_wscam183 | ST7789，240×284，RGB，无镜像 |
| 2 | assistant_wscam2 | ST7789，240×320，RGB，无镜像 |
| 2.8 | assistant_wscam28 | ST7789，240×320，RGB，无镜像 |
| 3.5 | assistant_wscam35 | ST7796，320×480，BGR，mirrorX |

四款均为SPI2 mode0／40MHz，MOSI1／CLK5／DC3／CS6，ESP面板RST为NC，LCD复位由CH32控制；实际板类不使用MISO，尽管config.h定义过GPIO2。均反色、无swapXY、gap0／0。2／2.8共用现有竖屏布局；1.83与3.5有各自尺寸布局，统一16px中文字体。没有按尺寸相近就套用同一色序、镜像或屏控。

四款独立profile均继承固定release默认16MB／Octal；没有把源码默认当作实际模组容量证明。

## CH32V003协议、功放与整端口边界

使用正式依赖 `waveshare/custom_io_expander_ch32v003: 2.0.0`，不把它替换成TCA9554或PCA9557，也不自行猜一个可硬件读回的寄存器协议。组件注册页指向的公开源码固定于Waveshare-ESP32-components提交 `9daffb7168b3c093a330da700988a22092294eef`：地址0x24，方向寄存器0x02、输出0x03、输入0x04、PWM0x05，API的I2C超时1000ms。其manifest嵌入的旧repository_info提交a601dda0对应路径返回HTTP404，因此本轮核对的是注册页公开快照与API，未确认canonical组件归档摘要、未安装依赖。

原理图引脚表与CH32器件连接一致：P0触摸复位、P1 LCD复位、P2 SD片选、P3 CAM_PWDN、P4 PA_CTRL、P5 BAT_EN、P6电源LED、P7 CHG_DET；PWM1为背光，功放为NS4150B。P6不是摄像头电源，P4也不是普通ESP GPIO。

厂家2.0.0构造函数会重写**整个端口**的direction=0xff、output=0x00；其read_direction／read_output回调只返回软件缓存，set_level可能因缓存值相同而不发I2C写入。本地保留该正式接口的初始化行为和限时调用，不能声称保留P2／P3／P5／P7、完成物理读回或确认实际功放电平。该边界已写在实现注释、registry、research和构建记录中。实际CH32固件版本、方向极性、整端口初始化对相机／SD／电池／充电检测的影响及电池接管，必须在设备上确认后才能接受该板。

板级封装 `waveshare_cam_board.cpp` 检查每个公开API返回结果；先请求PWM255保持背光暗、P4关闭，再按实际板类对P0／P1执行高10ms→低10ms→高10ms，P6电源LED置高。与上游启动即拉高P4不同，本地P4仅在实际DAC解除静音成功后使能；静音／关闭先请求关闭P4，编码器回调失败不继续使能。运行期功放和背光更新共用100ms限时互斥锁，不互相覆盖软件缓存。

背光没有ESP PWM GPIO，走CH32 `custom_io_expander_set_pwm`：`(100-percent)*255/100`，0%为255、100%为0。公开API返回成功只说明请求路径成功，不代表PWM或功放物理读回。新入口不触发普通SPI显示封装的ESP LEDC配置。启动显示／扩展器失败会尝试关功放、记录结果并停止本次启动，不跳转factory分区、不清配置、不建立panic重启循环。

## 音频与输入范围

四款I2C1共用SDA8／SCL7；ES8311输出、ES7210输入，I2S0 MCLK10／BCLK11／WS12／DIN13／DOUT14，输入输出24kHz。复用现有受检查的TDM4／64时钟帧，启用四个物理ADC、选slot0／MIC1单声道，软件播放参考AEC；不把原厂 `AUDIO_INPUT_REFERENCE=true` 当作本地硬件参考AEC已移植。底层data set_fmt／enable、MIC1增益和DAC mute结果沿用项目已有检查。

BOOT0首次释放后接已有按住说话和操作卡确认。没有注册LVGL触摸pointer，虽然初始化会释放P0触摸复位。原厂点击切换聊天、长按跳转factory和双击切换AEC未完整移植。OV5640／OV3660等相机初始化／拍摄／识物、SD缓存、电池／充电／关机、触摸驱动及其他外设都没有接入。

实际板卡与屏控修订、Flash／PSRAM、SPI信号／色序／窗口／背光、CH32固件及整端口行为、PA极性与静音／重开、24kHz时钟／TDM槽位／增益／软件AEC／唤醒、BOOT、联网／绑定／提醒及同板OTA仍待实机验收。现有实物只有微雪1.8，屏控修订未确认。特别是“相机未实现”不等于构造函数没有改变相机PWDN或电池控制位。

## 四次真实预检

四profile分别实际执行：

```powershell
& C:\Users\<user>\.espressif\python_env\idf5.5_py3.12_env\Scripts\python.exe -B tools/prepare_cached_firmware_build.py --profile <profile> --destination I:\xy_board_builds_20261109\<profile>-preflight-20261110 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6
```

全部退出1：`Required component is not installed: waveshare/custom_io_expander_ch32v003; nothing was staged`，目的目录均不存在，没有启动新板编译。3.5还声明 `espressif/esp_lcd_st7796: 1.4.0`，本机缓存也无此组件；预检在第一个CH32依赖处停止，第二项只是静态缺失记录，没有伪称同一次预检已验证全部依赖。

记录为 `native_dependency_preflight_20261110_ws_cam.json`；四目标 `dependency_resolution=blocked_missing_local_sources`、`build=not_run`。厂家组件只读查阅；本地只有调用API的板级封装，没有复制、安装或修改厂家组件源码。

## 既有板回归

旧 `espressif-esp32s3-korvo2-v3` 和 `waveshare-audio-board` 分别同步最终源码、刷新变化文件时间后实际执行 `idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`，均退出0。日志实际重编main、audio_io、waveshare_spi_audio和显示等公共输入；没有选择waveshare_cam_board或链接CH32构造函数，不能用来证明新控制协议、屏幕分支或3.5英寸ST7796可编译。

| 已有Profile | 应用字节 | 3MB应用分区剩余字节 |
| --- | ---: | ---: |
| espressif-esp32s3-korvo2-v3 | 2,260,096 | 885,632 |
| waveshare-audio-board | 2,256,096 | 889,632 |

两镜像实际执行 `python -B -m esptool --chip esp32s3 image_info --version 2 build/<app>.bin` 和 `xtensa-esp32s3-elf-nm.exe -C build/<app>.elf`，均退出0；独立项目名／16MB／checksum／validation hash匹配。仍分别链接Korvo2 ADC／双codec及微雪Audio Board TCA9555／JD9853／双codec路径。每份137输入摘要／130个main文件集合、编译宏、生成配置／合并defaults与当前工作区匹配，模型均2,203,819字节，在4MB模型分区内。

记录见 `cached_build_attempts_20261110_ws_cam_<profile>.json`；配置／日志／编译命令／依赖锁、产物／ELF、源快照与摘要在忽略目录 `evidence/ws-cam-builds-20261110/`。本机IDF5.5.6-dirty和17个既有组件override保持原样，Python offline_guard拒绝外网；这不是干净发行依赖构建。

固定源URL和SHA-256见 `board_research_v2.5.2.json` 的四变体共同finding；原文／官方原理图与渲染位于忽略目录 `evidence/ws-cam-source-20261110/`。未修改上游原理图、SDK或组件，未运行测试套件／独立类型检查，未安装依赖、启动宿主长期服务、Git写入、上传、发布、刷机或OTA。

实际执行 `python -B tools/sync_board_matrix.py --source-directory C:\Users\<user>\AppData\Local\Temp\xiaoyanzi-public-board-metadata-49b4380` 退出0：82官方源码变体／83本地profile含1历史入口／106待源码；累计47变体曾缓存编译通过。当前2 passed／45 stale／6历史依赖预检失败／135 not_run，其中29项not_run已有源码，全部188实机not_run。全量目标仍进行中。无屏六位绑定码人工决策函数、并发网关／运行时和自然插话文件未修改。

收尾实际 `git diff --check` 退出0；本批新增文件逐个 `git diff --no-index --check -- NUL <file>` 无空白错误（返回1为新增差异）。两份各137输入／16项归档构建产物、11项固定公开源证据及四个预检日志摘要匹配，矩阵研究／registry摘要与四新目标not_run状态匹配；原理图已完整及局部视觉核对，对照编辑前快照审阅本批diff。两项回归进程均已结束。
