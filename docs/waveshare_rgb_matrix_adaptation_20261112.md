# 微雪ESP32-S3-RGB-Matrix适配（2026-11-12）

范围：小智v2.5.2固定49b4380b3faa0ceadd28a3e78aaf0f311c46c57e的`waveshare-esp32-s3-rgb-matrix`默认64×64单面板语音终端。已建立实质源码入口；缺HUB75组件，尚未编译新目标，无Matrix镜像或实机验收。

## 已核对来源

24份来源及摘要见`evidence/rgb-matrix-source-20261112/source-artifacts.json`：小智板目录6文件及固定main/idf_component.yml；固定HUB75依赖14文件；微雪文档／资料页和0.3.6组件登记页3份。厂家文档确认32MB Flash／16MB PSRAM，本地不套N16R8假设。

固定main manifest声明`esphome/esp-hub75 ^0.3.6`；本地精确锁`0.3.6`。该版本组件登记页关联`esphome-libs/esp-hub75`提交`a087930a2d1d4b45f43dd8a0118a8804432ed11c`的`components/hub75`，许可MIT。读取的14份依赖代码仅在evidence作接口／资源调查，没有安装、复制成managed_component或链接参考文件。

S3后端明确使用LCD_CAM＋GDMA、LCD外设0 trigger，不占用I2S外设；音频沿用I2S0，因此不因库名或旧Arduino实现名称推断外设冲突。GDMA/DMA内部SRAM、持续刷新、FM6126A初始化仍需真实编译与实机。官方库单8bit64×64缓冲约57KB内部SRAM，本地LVGL帧另8192字节PSRAM。

## 源码入口

新增`waveshare-esp32-s3-rgb-matrix` → `assistant_wsmatrix`，仅`ASSISTANT_BOARD_MATRIX=1`。Flash配置32MB／Octal PSRAM，根分区只占前16MB：NVS9000/4000、双app20000/300000和320000/300000、model C00000/400000，后16MB不使用；不移动绑定NVS，不因Flash更大自动擦写其他范围。

- `matrix_display.cpp`只对接组件公共`Hub75Driver`，复用项目LVGL port；不是复制一套HUB75时序驱动。`matrix_board.h`独立声明音频I2C/I2S接线。
- 默认64×64，链1×1，STANDARD_TWO_SCAN（64px标准32行地址扫描）、FM6126A、20MHz、最低240Hz配置值。RGB1=4/5/6，RGB2=7/15/16，A18/B8/C3/D42/E9，CLK41/LAT40/OE2。实际刷新率未测量，配置240Hz不等于通过240Hz验收。
- OE低有效，启动先预装OE2高／PA11低、解除旧hold、启用pad读回。组件初始brightness0，begin后清黑图，再应用既有亮度偏好。开始失败时保留持久driver对象，复位OE pin输出路由并拉高消隐，PA低，停止启动；不假设库对部分初始化资源已完整释放。
- 一份64×64 RGB565 PSRAM整帧8192字节，LVGL FULL模式；同步`draw_pixels`返回后flush_ready，表示LVGL缓冲可复用，不能当作实物已显示证明。选择单缓冲与固定源相同，不启用可能无界等待的flip操作。真正刷新由驱动持续GDMA完成；不采用普通SPI LCD传输。
- 单独启用64px文本界面，不分配点云，保留14px中文字库／10px状态；绑定六位码专用Montserrat14且零字距，避免六位码被截成几位。标题PAIR/10m，操作说明滚动显示。
- 提醒和计时标题／正文滚动，底部VOICE/WEB提示；没有屏幕按钮、假BOOT短按长按说明或触摸命中区域。固定板类没有应用按键，本地不把未知GPIO0当实物可用键。说话、提醒／计时修改仍通过已有语音和控制台业务入口；本批没有创建手机本地portal，也没有把显示板冒充无屏板。
- 共享ES7210 TDM4取MIC1、ES8311 DAC16kHz，I2C0 SDA47/SCL48，MCLK12/BCLK43/WS38/DIN39/DOUT21，PA11高有效，显式GPIO读回并等实际DAC解除静音后开启。实际网络24→16FIR和相同PCM软件AEC复用既有管道；不宣称MIC3/4硬件回采或双麦波束已验收。
- UART0 TX43与音频BCLK43冲突，选择既有USB Serial/JTAG console；BOOT仍是硬件下载用途，不因源码profile存在宣称机身有应用交互键。
- 不复制原厂将音量0写回70的NVS偏好覆盖；不改绑定凭据／音乐授权／初始唤醒门禁，不初始化SD、RTC、IMU、相机、电量或充电策略，不接多面板链和睡眠关机。

## 依赖预检与构建边界

实际命令：`python -B tools/prepare_cached_firmware_build.py --profile waveshare-esp32-s3-rgb-matrix --destination I:\xy_board_builds_20261109\matrix_preflight_20261112 --idf-path C:\Espressif\official-5.5.6\frameworks\esp-idf-v5.5.6`。

实际退出1：`Required component is not installed: esphome/esp-hub75; nothing was staged`。目标目录不存在，未启动目标编译，无Matrix固件。记录`native_dependency_preflight_20261112_rgb_matrix.json`及原始日志／SHA256。依赖安装由牛牛执行；本次仅修改依赖清单、读取固定公共参考，没有安装。

旧RLCD与微雪Audio Board最终源码reconfigure build均退出0，两份esptool image_info、nm、size和gen_esp32part检查均退出0。

| 旧入口 | app字节 | 3MB app余量 | model字节／容量 |
| --- | ---: | ---: | ---: |
| RLCD `assistant_wsrlcd42` | 2,227,808 | 917,920 | 2,203,819／4,194,304 |
| Audio Board `assistant_wsaudio` | 2,256,176 | 889,552 | 2,203,819／4,194,304 |

实际应用身份／16MB Flash／checksum及hash有效，解码分区保留NVS与双app/model且均在容量内。每项195个main文件集合、202份输入摘要与最终工作区一致，各21份归档产物共42份SHA256匹配。编译命令和ELF明确没有选择matrix_display.cpp或Hub75Driver，不能给Matrix目标记passed。

小屏六位码另读本机Montserrat14源字形度量：数字最大步进150/16px，六位最大步进合计56.25px，小于64px；这是字体布局证据，不是运行或实机验收。库扫描／显示队列／亮度／颜色、GPIO11功放与32MB配置均仍未编译。

记录：`cached_build_attempts_20261112_rgb_matrix_*.json`和`evidence/rgb-matrix-source-20261112/`。回归命令是`xy_rgb_matrix_regression_20261112.ps1 -Stage <各stage> -Board <profile>`，同步全部最终main／registry/defaults后`idf.py -DIDF_TARGET=esp32s3 -DASSISTANT_BOARD=<profile> reconfigure build`。旧入口不会选中`matrix_display.cpp`、32MB配置、64px矩阵专用UI／GPIO11功放分支；成功也不能当新目标编译证据。

## 未完成项

只有现有微雪1.8实物，Matrix完整依赖构建、32MB/16MB实际修订、64×64 FM6126A扫描接线／颜色、持续DMA内存与LCD_CAM-I2S并行、240Hz实际刷新／OE启动亮度、六位码和滚动文字实际可读性、无按键时语音控制／提醒／配网绑定／OTA均未验收。组件begin/is_running只是软件状态，无法证明真实屏幕／音频工作。

没有运行测试套件、安装依赖、Git写操作、上传发布、刷机或OTA。
